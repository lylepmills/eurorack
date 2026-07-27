/*
 * TCG plugin: count what an engine actually executes on a Cortex-M4.
 *
 * Instruction count alone repeats the mistake the host-side check made -- it
 * says nothing about where the data came from. On this chip the wavetables live
 * in FLASH (wait-stated, no data cache, contending with instruction fetch),
 * while stack and state live in single-cycle SRAM, so two engines with equal
 * instruction counts can differ severalfold in real cost.
 *
 * So reads are classified by address. The harness is linked with .text/.rodata
 * in the low region and .data/.bss/stack in the 0x20000000 region, mirroring the
 * real device's flash/SRAM split -- a read below the RAM base is a read of code
 * or constant tables, i.e. one that would hit flash on hardware.
 *
 * Output feeds a cost model of the form
 *     cycles ~= a*instructions + b*flash_reads + c*ram_accesses
 * whose coefficients are fitted against real DWT measurements taken by
 * plaits/cpu_probe.h on the module itself.
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* Anything at or above this is SRAM on the MPS2 board (and on the STM32). */
#define RAM_BASE 0x20000000ULL

static uint64_t n_insn;
/* Instructions QEMU counts as one but the hardware pays many cycles for, and
   which are therefore invisible to a pure instruction count: VDIV and VSQRT are
   ~14 cycles each on this FPU and are NOT pipelined. */
static uint64_t n_div;
static uint64_t n_sqrt;
static uint64_t n_read_flash;
static uint64_t n_read_ram;
static uint64_t n_write;

static void vcpu_insn_exec(unsigned int cpu_index, void *udata)
{
    n_insn++;
}

static void vcpu_div_exec(unsigned int cpu_index, void *udata)
{
    n_div++;
}

static void vcpu_sqrt_exec(unsigned int cpu_index, void *udata)
{
    n_sqrt++;
}

static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                     uint64_t vaddr, void *udata)
{
    if (qemu_plugin_mem_is_store(info)) {
        n_write++;
    } else if (vaddr >= RAM_BASE) {
        n_read_ram++;
    } else {
        n_read_flash++;
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec,
                                               QEMU_PLUGIN_CB_NO_REGS, NULL);
        /* Classify at TRANSLATION time -- once per instruction, not per
           execution -- so the extra counters cost nothing at run time. */
        char *disas = qemu_plugin_insn_disas(insn);
        if (disas) {
            if (strncmp(disas, "vdiv", 4) == 0) {
                qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_div_exec,
                                                       QEMU_PLUGIN_CB_NO_REGS, NULL);
            } else if (strncmp(disas, "vsqrt", 5) == 0) {
                qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_sqrt_exec,
                                                       QEMU_PLUGIN_CB_NO_REGS, NULL);
            }
            g_free(disas);
        }
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         QEMU_PLUGIN_MEM_RW, NULL);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    /* Machine-readable single line; the SDK parses this. */
    fprintf(stderr,
            "PLAITS_QEMU_COUNTS insns=%" PRIu64 " flash_reads=%" PRIu64
            " ram_reads=%" PRIu64 " writes=%" PRIu64 " divs=%" PRIu64 " sqrts=%" PRIu64 "\n",
            n_insn, n_read_flash, n_read_ram, n_write, n_div, n_sqrt);
    fflush(stderr);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
