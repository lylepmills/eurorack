#!/bin/bash
# Flash a Palette Seed3 image over USB DFU (the Seed3's on-board USB-C).
#
#   ./flash.sh boot                 one-time: put the Daisy bootloader (v6.4, DFU on the
#                                   on-board port, 2 s grace) in internal flash. The Seed3
#                                   must be in the ST ROM DFU: hold BOOT, tap RESET, release.
#   ./flash.sh app <image.bin>      flash a BOOT_QSPI / BOOT_SRAM app to 0x90040000. The
#                                   Daisy bootloader must be in its grace period (LED pulsing,
#                                   2.5 s after reset; press BOOT during it to hold it there).
#   ./flash.sh internal <image.bin> flash a BOOT_NONE image to internal flash (ROM DFU, as
#                                   for `boot`). Overwrites the Daisy bootloader.
#   ./flash.sh list                 show DFU devices (0483:df11 = ROM DFU or Daisy bootloader)
#
# After `app`, the board leaves DFU and runs the image; open the serial log with
#   tools/capture_bench.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
BOOT_BIN=$HERE/libDaisy/core/dsy_bootloader_v6_4-intdfu-2000ms.bin
case "${1:-}" in
  list)     dfu-util -l ;;
  boot)     dfu-util -a 0 -s 0x08000000:leave -D "$BOOT_BIN" -d ,0483:df11 ;;
  internal) dfu-util -a 0 -s 0x08000000:leave -D "${2:?image.bin}" -d ,0483:df11 ;;
  app)      dfu-util -a 0 -s 0x90040000:leave -D "${2:?image.bin}" -d ,0483:df11 ;;
  *) sed -n 2,17p "$0"; exit 2 ;;
esac
