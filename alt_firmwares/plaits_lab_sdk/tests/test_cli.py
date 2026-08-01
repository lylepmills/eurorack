from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import sys
import shutil
import tempfile
import unittest
import zipfile
from contextlib import redirect_stdout
from pathlib import Path
from types import SimpleNamespace


SDK_DIR = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("plaits_lab", SDK_DIR / "plaits_lab.py")
assert SPEC and SPEC.loader
plaits_lab = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(plaits_lab)


def host_can_sanitize() -> bool:
    """Probe guard for tests that need a real sanitized build. Absent any host
    compiler at all the probe raises, which is still 'cannot sanitize' here."""
    try:
        return plaits_lab.host_sanitizers_available(None)
    except plaits_lab.PackageError:
        return False


# These stand in for contributor-written sources, so they carry the same SPDX
# header load_package() now requires of every community source file.
CHORD_PROBE_HEADER = """\
// Copyright 2026 Test Author.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_LAB_CHORD_PROBE_ENGINE_H_
#define PLAITS_LAB_CHORD_PROBE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/chords/chord_bank.h"

namespace plaits {

class ChordProbeEngine : public Engine {
 public:
  ChordProbeEngine() { }
  ~ChordProbeEngine() { }
  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out, float* aux, size_t size, bool* already_enveloped);

 private:
  ChordBank chords_;
  DISALLOW_COPY_AND_ASSIGN(ChordProbeEngine);
};

}  // namespace plaits

#endif  // PLAITS_LAB_CHORD_PROBE_ENGINE_H_
"""

CHORD_PROBE_IMPL = """\
// Copyright 2026 Test Author.
// SPDX-License-Identifier: MIT

#include "chord-probe_engine.h"

namespace plaits {

void ChordProbeEngine::Init(stmlib::BufferAllocator* allocator) {
  chords_.Init(allocator);
  Reset();
}

void ChordProbeEngine::Reset() {
  chords_.Reset();
}

void ChordProbeEngine::Render(const EngineParameters& parameters,
    float* out, float* aux, size_t size, bool* already_enveloped) {
  chords_.set_chord(parameters.harmonics, 0);
  for (size_t i = 0; i < size; ++i) {
    out[i] = 0.0f;
    aux[i] = 0.0f;
  }
  *already_enveloped = false;
}

}  // namespace plaits
"""


class PackageTests(unittest.TestCase):
    def test_reference_packages_validate(self) -> None:
        packages = [
            SDK_DIR / "packages" / "mutable-instruments" / "virtual-analog",
            SDK_DIR / "packages" / "rubato" / "pulsar",
        ]
        for package in packages:
            with self.subTest(package=package):
                loaded = plaits_lab.load_package(str(package))
                self.assertEqual(loaded["manifest"]["schemaVersion"], 1)
                self.assertEqual(len(loaded["manifest"]["controls"]), 4)
                self.assertTrue(loaded["source_files"])

    def test_scenario_rejects_out_of_range_controls(self) -> None:
        scenario = {
            "id": "bad",
            "name": "Bad",
            "durationSeconds": 1,
            "note": 60,
            "triggerHz": 0,
            "controls": {
                "harmonics": [0, 2],
                "timbre": [0, 1],
                "morph": [0, 1],
                "macro": [0, 1],
            },
        }
        with self.assertRaises(plaits_lab.PackageError):
            plaits_lab.validate_scenario(scenario, 0)

    def test_path_escape_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            base = Path(temp_dir)
            with self.assertRaises(plaits_lab.PackageError):
                plaits_lab.resolve_within(base, "../outside.cc", "test path")

    def test_community_policy_rejects_hardware_access(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "unsafe.cc"
            source.write_text("void Render() { HAL_GPIO_WritePin(); }\n", encoding="utf-8")
            with self.assertRaises(plaits_lab.PackageError):
                plaits_lab.validate_community_source([source])

    def test_source_policy_failure_points_at_the_line(self) -> None:
        # A rejected engine should tell the contributor WHERE and HOW to fix it,
        # not just name the rule category.
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "leaky.cc"
            source.write_text("// header\nvoid Init() {\n  buffer = new float[64];\n}\n", encoding="utf-8")
            with self.assertRaises(plaits_lab.PackageError) as ctx:
                plaits_lab.validate_community_source([source])
            message = str(ctx.exception)
            self.assertIn("leaky.cc:3", message)          # the offending line
            self.assertIn("dynamic allocation", message)  # the category
            self.assertIn("BufferAllocator", message)     # the fix hint

    def test_check_flags_cstdint_for_cxx98(self) -> None:
        # <cstdint> needs C++11; the firmware is C++98. check should point at
        # <stdint.h>, not the generic "non-SDK header" rejection.
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "types.cc"
            source.write_text('#include <cstdint>\nuint32_t f() { return 0; }\n', encoding="utf-8")
            with self.assertRaises(plaits_lab.PackageError) as ctx:
                plaits_lab.validate_community_source([source])
            message = str(ctx.exception)
            self.assertIn("<cstdint>", message)
            self.assertIn("C++98", message)
            self.assertIn("<stdint.h>", message)
        # <stdint.h> (the C++98-safe header) is fine.
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "ok.cc"
            source.write_text('#include <stdint.h>\nuint32_t f() { return 0; }\n', encoding="utf-8")
            plaits_lab.validate_community_source([source])

    def test_check_flags_libm_transcendentals(self) -> None:
        # libm transcendentals compile on the host but can't link on the bare-metal
        # firmware; check catches them at validation with the shared-LUT fix (NOT
        # std::log/std::exp, which are libm too).
        for call, symbol in [
            ("std::log2(x)", "std::log2"), ("std::exp2(x)", "std::exp2"),
            ("std::sin(x)", "std::sin"), ("std::exp(x)", "std::exp"),
            ("std::pow(x, 2.0f)", "std::pow"), ("std::log(x)", "std::log"),
        ]:
            with tempfile.TemporaryDirectory() as temp_dir:
                source = Path(temp_dir) / "pitchy.cc"
                source.write_text(f"float f(float x) {{\n  return {call};\n}}\n", encoding="utf-8")
                with self.assertRaises(plaits_lab.PackageError) as ctx:
                    plaits_lab.validate_community_source([source])
                message = str(ctx.exception)
                self.assertIn("pitchy.cc:2", message)
                self.assertIn(symbol, message)
                self.assertIn("can't link", message)
                self.assertIn("plaits::Sine", message)          # the real portable fix
                self.assertNotIn("std::log(x)", message)         # never suggest more libm
        # std::log must not shadow std::log2 (word-boundary): report the exact call.
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "only_log2.cc"
            source.write_text("float f(float x) { return std::log2(x); }\n", encoding="utf-8")
            with self.assertRaises(plaits_lab.PackageError) as ctx:
                plaits_lab.validate_community_source([source])
            self.assertIn("std::log2", str(ctx.exception))
        # A comment mentioning it must not trip the check (comments are stripped).
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "commented.cc"
            source.write_text("// avoid std::sin here\nfloat f() { return 0.0f; }\n", encoding="utf-8")
            plaits_lab.validate_community_source([source])

    def test_check_arm_skips_reference_packages(self) -> None:
        # Reference packages already build on the ARM toolchain; --arm is a no-op
        # for them (no toolchain/docker needed, no error).
        package = plaits_lab.builtin_package("chords")
        plaits_lab.arm_compile_check(
            package, SimpleNamespace(toolchain="/nonexistent", docker_image="x", native=False))

    def test_cost_model_validates_against_held_out_engines(self) -> None:
        # The guard that matters. A model reproducing its own fit inputs proves
        # nothing; this predicts each engine from a fit that EXCLUDES it, which
        # is what makes a wrong model visible instead of self-confirming.
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "cost_model", Path(plaits_lab.__file__).parent / "qemu/cost_model.py")
        cost_model = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cost_model)

        rows = cost_model.leave_one_out()
        ordinary = [abs(r["error_pct"]) for r in rows if not r["outlier"]]
        self.assertGreaterEqual(len(ordinary), 12)
        mean_error = sum(ordinary) / len(ordinary)
        # Held-out accuracy, measured: mean ~14%, worst ~30%. Loosened slightly
        # so re-measuring does not fail the suite, but tight enough that a model
        # regression does.
        self.assertLess(mean_error, 20.0, f"held-out mean error regressed to {mean_error:.1f}%")
        self.assertTrue(all(e < 45.0 for e in ordinary), "an ordinary engine mispredicted badly")

        model = cost_model.CostModel()
        self.assertGreater(model.k_mid, 2.0)
        self.assertLess(model.k_mid, 5.0)
        self.assertLess(model.k_low, model.k_high)
        # The estimate must never read as certainty.
        self.assertIn("hardware", model.outlier_note().lower())

    def test_cost_model_verdict_is_conservative(self) -> None:
        # An engine is only called OK when the TOP of its band fits: being wrong
        # optimistically is what ships something broken.
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "cost_model", Path(plaits_lab.__file__).parent / "qemu/cost_model.py")
        cost_model = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cost_model)
        model = cost_model.CostModel()
        self.assertEqual(model.verdict(100)[0], "OK")     # ~10% of budget
        self.assertEqual(model.verdict(1000)[0], "FAIL")  # ~200% of budget
        borderline = model.verdict(460)[0]                # modal-resonator, ~86% measured
        self.assertIn(borderline, ("WARN", "FAIL"))

    def test_cpu_probe_build_is_opt_in_and_off_by_default(self) -> None:
        # The probe measures Voice::Render with the Cortex-M4 cycle counter, so
        # it must never appear unasked.
        with tempfile.TemporaryDirectory() as temp_dir:
            pkg_dir = Path(temp_dir) / "probed"
            with redirect_stdout(io.StringIO()):
                plaits_lab.init_command(SimpleNamespace(
                    output=str(pkg_dir), from_engine="blank", author="T",
                    package_id="test-author/probed", slug="probed", name="Probed"))
            package = plaits_lab.load_package(str(pkg_dir))
            self.assertNotIn("PLAITS_CPU_PROBE",
                             plaits_lab.render_local_hardware_config(package))
            self.assertIn("#define PLAITS_CPU_PROBE 1",
                          plaits_lab.render_local_hardware_config(package, cpu_probe=True))

    def test_cpu_probe_leaves_aux_alone_unless_asked(self) -> None:
        # The two readout channels are separate flags. A contributor's probe is
        # the LED meter: it costs them nothing, while the AUX tone OVERWRITES
        # their engine's second output — so the tone only appears on request
        # (or for memhunt, whose readout is the tone).
        with tempfile.TemporaryDirectory() as temp_dir:
            pkg_dir = Path(temp_dir) / "auxed"
            with redirect_stdout(io.StringIO()):
                plaits_lab.init_command(SimpleNamespace(
                    output=str(pkg_dir), from_engine="blank", author="T",
                    package_id="test-author/auxed", slug="auxed", name="Auxed"))
            package = plaits_lab.load_package(str(pkg_dir))
            render = plaits_lab.render_local_hardware_config

            self.assertIn("#define PLAITS_CPU_PROBE_AUX 0",
                          render(package, cpu_probe=True))
            self.assertIn("#define PLAITS_CPU_PROBE_AUX 1",
                          render(package, cpu_probe=True, cpu_probe_aux=True))
            # --cpu-probe-aux is a probe build in its own right.
            self.assertIn("#define PLAITS_CPU_PROBE 1",
                          render(package, cpu_probe_aux=True))
            # memhunt has no other channel to report on.
            self.assertIn("#define PLAITS_CPU_PROBE_AUX 1",
                          render(package, cpu_probe=True, memhunt=True))
            # The bench firmware is the mirror image: tone, no LED meter.
            bench = plaits_lab.render_stock_bench_config()
            self.assertIn("#define PLAITS_CPU_PROBE_LEDS 0", bench)
            self.assertIn("#define PLAITS_CPU_PROBE_AUX 1", bench)

    def test_cpu_reference_ratio_divides_package_cost_by_stock_cost(self) -> None:
        # The ratio is the whole point: absolute host timings can't tell you
        # whether an engine fits on a 72 MHz module, but "N times a stock engine
        # built by the same harness" cancels the machine out.
        with tempfile.TemporaryDirectory() as temp_dir:
            pkg_dir = Path(temp_dir) / "costly"
            with redirect_stdout(io.StringIO()):
                plaits_lab.init_command(SimpleNamespace(
                    output=str(pkg_dir), from_engine="blank", author="T",
                    package_id="test-author/costly", slug="costly", name="Costly"))
            package = plaits_lab.load_package(str(pkg_dir))
            built: list[str] = []
            real_compile, real_measure = plaits_lab.compile_cpu_bench, plaits_lab.measure_cpu_cost
            plaits_lab.compile_cpu_bench = (
                lambda units, header, cls, includes, output, compiler:
                    built.append(Path(output).name))
            # 180 ns for the package, 30 ns for stock -> 6.0x
            plaits_lab.measure_cpu_cost = (
                lambda binary: 180.0 if "package" in Path(binary).name else 30.0)
            try:
                cost = plaits_lab.cpu_reference_ratio(package, None, Path(temp_dir))
            finally:
                plaits_lab.compile_cpu_bench, plaits_lab.measure_cpu_cost = real_compile, real_measure
            self.assertEqual(cost["reference"], plaits_lab.CPU_REFERENCE_ENGINE)
            self.assertAlmostEqual(cost["ratio"], 6.0)
            # Both the package AND the stock reference are built by this harness.
            self.assertEqual(len(built), 2)

    def test_cpu_check_fails_an_engine_that_cannot_fit(self) -> None:
        # Regression for the community engine that passed every other check at
        # ~2.3x the heaviest stock engine and then starved the module: glitched
        # audio, and a UI loop too short of cycles to refresh the LEDs. This is
        # the measured failure point the fail threshold is anchored on.
        with self.assertRaises(plaits_lab.PackageError) as caught:
            plaits_lab.report_cpu_reference_ratio(
                {"reference": "two-op-fm", "packageNs": 216.0, "referenceNs": 96.0, "ratio": 2.3})
        message = str(caught.exception)
        self.assertIn("2.3x", message)
        self.assertIn("per SAMPLE", message)   # names the cause
        self.assertIn("per BLOCK", message)    # and the fix

    def test_cpu_check_warns_but_passes_above_the_reference(self) -> None:
        out = io.StringIO()
        with redirect_stdout(out):
            plaits_lab.report_cpu_reference_ratio(
                {"reference": "two-op-fm", "packageNs": 130.0, "referenceNs": 96.0, "ratio": 1.35})
        rendered = out.getvalue()
        self.assertIn("⚠ CPU cost", rendered)
        # The caveat is the point: a host timing must never read as a hardware
        # verdict, so it always names the estimator and the on-module probe.
        self.assertIn("does NOT predict hardware cost", rendered)
        self.assertIn("--cpu-probe", rendered)

    def test_cpu_check_passes_below_the_heaviest_stock_engine(self) -> None:
        # Cheaper than an engine Mutable ships => the module provably has room.
        out = io.StringIO()
        with redirect_stdout(out):
            plaits_lab.report_cpu_reference_ratio(
                {"reference": "two-op-fm", "packageNs": 58.0, "referenceNs": 96.0, "ratio": 0.61})
        self.assertIn("✓ CPU cost", out.getvalue())

    def test_cpu_check_is_advisory_when_unmeasurable(self) -> None:
        # An unavailable measurement must never block a contributor.
        out = io.StringIO()
        with redirect_stdout(out):
            plaits_lab.report_cpu_reference_ratio(None)
        self.assertEqual(out.getvalue(), "")

    def test_dedupe_units_keeps_first_seen_order(self) -> None:
        root = Path(plaits_lab.__file__).parent
        units = [root / "cpu_bench.cc", root / "render_model.cc", root / "cpu_bench.cc"]
        deduped = plaits_lab.dedupe_units(units)
        self.assertEqual([Path(item).name for item in deduped],
                         ["cpu_bench.cc", "render_model.cc"])

    def test_check_arm_dispatches_to_builder_container(self) -> None:
        # With no local ARM toolchain, --arm compiles the package via the builder
        # image — a compile-only 'check', not a full firmware build.
        with tempfile.TemporaryDirectory() as temp_dir:
            pkg_dir = Path(temp_dir) / "armed"
            with redirect_stdout(io.StringIO()):
                plaits_lab.init_command(SimpleNamespace(
                    output=str(pkg_dir), from_engine="blank", author="T",
                    package_id="test-author/armed", slug="armed", name="Armed"))
            package = plaits_lab.load_package(str(pkg_dir))
            args = SimpleNamespace(toolchain="/nonexistent-arm", docker_image="img:test", native=False)
            captured: dict[str, list[str]] = {}
            real_run, real_which = plaits_lab.subprocess.run, plaits_lab.shutil.which
            plaits_lab.shutil.which = lambda name: "/usr/bin/docker" if name == "docker" else real_which(name)
            # The size is measured INSIDE the container; the mock returns it on the
            # container's stdout, and the Docker path must surface it (not swallow it).
            plaits_lab.subprocess.run = lambda cmd, **kw: (
                captured.__setitem__("cmd", cmd)
                or SimpleNamespace(returncode=0, stdout="  model size: 1,234 bytes of flash\n", stderr=""))
            out = io.StringIO()
            try:
                with redirect_stdout(out):
                    plaits_lab.arm_compile_check(package, args)
            finally:
                plaits_lab.subprocess.run, plaits_lab.shutil.which = real_run, real_which
            self.assertIn("model size: 1,234 bytes", out.getvalue())  # surfaced, not swallowed
            joined = " ".join(captured["cmd"])
            self.assertIn("img:test", joined)                   # the builder image
            self.assertIn("check /contributor --arm", joined)   # compile-only check, not `build`
            self.assertIn("--native", joined)                   # runs the compile inside the container
            self.assertNotIn("--hardware", joined)              # NOT a full firmware build

    def test_local_hardware_config_carries_only_the_contributor_engine(self) -> None:
        # The local build registers ONLY the community engine, so the linker strips
        # the stock palette and hands its flash to the contributor.
        with tempfile.TemporaryDirectory() as temp_dir:
            pkg_dir = Path(temp_dir) / "solo"
            with redirect_stdout(io.StringIO()):
                plaits_lab.init_command(SimpleNamespace(
                    output=str(pkg_dir), from_engine="blank", author="T",
                    package_id="test-author/solo", slug="solo", name="Solo"))
            config = plaits_lab.render_local_hardware_config(plaits_lab.load_package(str(pkg_dir)))
            self.assertEqual(config.count("RegisterInstance"), 1)
            self.assertIn("solo_engine.h", config)
            self.assertNotIn("virtual_analog", config.lower())
            # The navigation config must MATCH the single registration, or the
            # firmware keeps its default 24-slot / 3-bank layout and exposes null
            # engines. Keep 3 banks (a <3-bank array is untested): the tested
            # single-populated-bank pattern {N,0,0}.
            self.assertIn("#define PLAITS_ENGINE_COUNT 1", config)
            self.assertIn("#define PLAITS_BANK_SIZES { 1, 0, 0 }", config)
            self.assertIn("#define PLAITS_ENGINE_ROWS { 0 }", config)

    def test_hardware_build_reports_host_output_path(self) -> None:
        # The container writes the WAV to /output/<name> (a mount of the host dir);
        # the message must show the HOST path, or the user can't find the file.
        with tempfile.TemporaryDirectory() as temp_dir:
            pkg_dir = Path(temp_dir) / "hw"
            with redirect_stdout(io.StringIO()):
                plaits_lab.init_command(SimpleNamespace(
                    output=str(pkg_dir), from_engine="blank", author="T",
                    package_id="test-author/hw", slug="hw", name="Hw"))
            out_wav = Path(temp_dir) / "hw-firmware.wav"
            args = SimpleNamespace(package=str(pkg_dir), output=str(out_wav),
                                   toolchain="/nonexistent-arm", docker_image="img:test", native=False)
            real_run, real_which = plaits_lab.subprocess.run, plaits_lab.shutil.which
            plaits_lab.shutil.which = lambda name: "/usr/bin/docker" if name == "docker" else real_which(name)
            plaits_lab.subprocess.run = lambda cmd, **kw: SimpleNamespace(
                returncode=0, stdout=f"built UNREVIEWED local firmware /output/{out_wav.name}\n", stderr="")
            printed = io.StringIO()
            try:
                with redirect_stdout(printed):
                    plaits_lab.hardware_build_command(args)
            finally:
                plaits_lab.subprocess.run, plaits_lab.shutil.which = real_run, real_which
            text = printed.getvalue()
            self.assertIn(str(out_wav.resolve()), text)              # the real host path
            self.assertNotIn(f"/output/{out_wav.name}", text)        # not the container path

    def test_arm_flash_footprint_sums_text_and_data(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            tool = Path(temp_dir) / "arm-none-eabi-size"; tool.write_text("#!/bin/sh\n")
            obj = Path(temp_dir) / "x.o"; obj.write_text("")
            real_run = plaits_lab.subprocess.run
            plaits_lab.subprocess.run = lambda cmd, **kw: SimpleNamespace(
                returncode=0, stdout="   text    data     bss     dec\n   1200      48      16    1264\n", stderr="")
            try:
                flash = plaits_lab.arm_flash_footprint(tool, [str(obj)])
            finally:
                plaits_lab.subprocess.run = real_run
            self.assertEqual(flash, 1248)  # text 1200 + data 48
        self.assertIsNone(plaits_lab.arm_flash_footprint(Path("/nonexistent-size"), []))

    def test_audio_health_messages_are_actionable(self) -> None:
        import wave as wave_module

        def write_wav(path: Path, sample_fn) -> None:
            with wave_module.open(str(path), "wb") as out:
                out.setnchannels(2); out.setsampwidth(2); out.setframerate(48000)
                frames = bytearray()
                for i in range(48000):  # 1 second
                    v = max(-32768, min(32767, int(sample_fn(i))))
                    frames += int(v).to_bytes(2, "little", signed=True) * 2
                out.writeframes(bytes(frames))
        with tempfile.TemporaryDirectory() as temp_dir:
            silent = Path(temp_dir) / "silent.wav"
            write_wav(silent, lambda i: 0)
            with self.assertRaises(plaits_lab.PackageError) as ctx:
                plaits_lab.analyze_wav(silent, 1.0, 0.1)
            self.assertIn("silent", str(ctx.exception))
            self.assertIn("Render()", str(ctx.exception))

            biased = Path(temp_dir) / "biased.wav"
            write_wav(biased, lambda i: 16000)  # large constant DC offset
            with self.assertRaises(plaits_lab.PackageError) as ctx:
                plaits_lab.analyze_wav(biased, 1.0, 0.1)
            self.assertIn("DC offset", str(ctx.exception))
            self.assertIn("center the waveform", str(ctx.exception))

    def test_authoritative_catalog_exposes_every_forkable_model(self) -> None:
        catalog, public = plaits_lab.load_builtin_catalog()
        self.assertEqual(len(catalog), 81)
        self.assertEqual(set(catalog), set(public))
        self.assertTrue(all(item["digest"].startswith("sha256:") for item in public.values()))

    def test_every_catalog_model_can_be_forked(self) -> None:
        catalog, _ = plaits_lab.load_builtin_catalog()
        with tempfile.TemporaryDirectory() as temp_dir:
            for engine_id in catalog:
                with self.subTest(engine=engine_id):
                    slug = f"{engine_id}-fork"
                    output = Path(temp_dir) / slug
                    with redirect_stdout(io.StringIO()):
                        plaits_lab.init_command(SimpleNamespace(
                            output=str(output), from_engine=engine_id,
                            author="Test Author", package_id=f"test-author/{slug}",
                            slug=slug, name=None,
                        ))
                    loaded = plaits_lab.load_package(str(output))
                    self.assertEqual(loaded["manifest"]["forkedFrom"], engine_id)

    def test_source_policy_ignores_comments_but_not_code(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "comments.cc"
            source.write_text(
                "// a new capture window\n/* delete old notes */\nvoid Render() {}\n",
                encoding="utf-8",
            )
            plaits_lab.validate_community_source([source])
            source.write_text("void Render() { delete pointer; }\n", encoding="utf-8")
            with self.assertRaises(plaits_lab.PackageError):
                plaits_lab.validate_community_source([source])

    @unittest.skipUnless(shutil.which("c++") or shutil.which("g++"), "host C++ compiler required")
    def test_blank_package_can_be_validated_and_bundled(self) -> None:
        # Exercises the HOST bundling path end to end, so it needs a toolchain
        # that can actually link the sanitizers — MinGW-w64 cannot, and there the
        # real submit delegates to Docker instead (covered separately below).
        if not host_can_sanitize():
            self.skipTest("host C++ toolchain ships no sanitizer runtime")
        with tempfile.TemporaryDirectory() as temp_dir:
            package = Path(temp_dir) / "bright-wave"
            plaits_lab.init_command(SimpleNamespace(
                output=str(package), from_engine="blank", author="Test Author",
                package_id="test-author/bright-wave", slug="bright-wave", name="Bright Wave",
            ))
            loaded = plaits_lab.load_package(str(package))
            self.assertEqual(loaded["manifest"]["packageType"], "community")

            bundle = Path(temp_dir) / "bright-wave.zip"
            # --bundle-only: this test is about the zip the gauntlet produces,
            # not about uploading it. Without it, submit runs its real upload.
            plaits_lab.submit_command(SimpleNamespace(
                package=str(package), output=str(bundle), compiler=None,
                native=True, docker_image="unused", bundle_only=True,
            ))
            with zipfile.ZipFile(bundle) as archive:
                submission = json.loads(archive.read("submission.json"))
                self.assertEqual(submission["state"], "draft")
                self.assertEqual(set(submission["audioAnalysis"]), {"hero", "triggered"})
                self.assertIn("package/src/bright-wave_engine.cc", archive.namelist())

    def test_shared_module_include_requires_declaration(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "chordy.cc"
            source.write_text(
                '#include "plaits/dsp/chords/chord_bank.h"\nvoid Render() {}\n',
                encoding="utf-8",
            )
            with self.assertRaises(plaits_lab.PackageError) as context:
                plaits_lab.validate_community_source([source])
            self.assertIn("sharedModules", str(context.exception))
            # Declaring the owning module makes the identical source pass.
            plaits_lab.validate_community_source([source], frozenset({"chord-bank"}))

    def test_autodeclare_writes_missing_shared_module(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            package_dir = Path(temp_dir) / "chord-probe"
            with redirect_stdout(io.StringIO()):
                plaits_lab.init_command(SimpleNamespace(
                    output=str(package_dir), from_engine="blank", author="Test Author",
                    package_id="test-author/chord-probe", slug="chord-probe", name="Chord Probe",
                ))
            (package_dir / "src" / "chord-probe_engine.h").write_text(CHORD_PROBE_HEADER, encoding="utf-8")
            (package_dir / "src" / "chord-probe_engine.cc").write_text(CHORD_PROBE_IMPL, encoding="utf-8")
            # By default, including the header without declaring still errors.
            with self.assertRaises(plaits_lab.PackageError):
                plaits_lab.load_package(str(package_dir))
            # autodeclare=True adds the module and rewrites the manifest on disk.
            package = plaits_lab.load_package(str(package_dir), autodeclare=True)
            self.assertEqual(package["autodeclared"], ["chord-bank"])
            manifest = json.loads((package_dir / "plaits-engine.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["sharedModules"], ["chord-bank"])
            # Idempotent — a second pass finds nothing new to declare.
            again = plaits_lab.load_package(str(package_dir), autodeclare=True)
            self.assertEqual(again["autodeclared"], [])

    def test_forking_chord_engine_declares_shared_module(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / "chords-fork"
            with redirect_stdout(io.StringIO()):
                plaits_lab.init_command(SimpleNamespace(
                    output=str(output), from_engine="chords",
                    author="Test Author", package_id="test-author/chords-fork",
                    slug="chords-fork", name=None,
                ))
            loaded = plaits_lab.load_package(str(output))
            self.assertEqual(loaded["manifest"]["sharedModules"], ["chord-bank"])
            # The fork vendors only its own engine file; chord_bank is a module.
            self.assertEqual(
                [path.name for path in loaded["source_files"]], ["chords-fork_engine.cc"],
            )

    def test_unknown_shared_module_is_rejected(self) -> None:
        with self.assertRaises(plaits_lab.PackageError):
            plaits_lab.validate_shared_modules(["not-a-real-module"])

    def _scaffold(self, directory: Path, slug: str, **overrides) -> Path:
        """Scaffold a package and return its directory (init_command prints)."""
        output = directory / slug
        args = {
            "output": str(output), "from_engine": "blank", "author": "Ada Lovelace",
            "license": "MIT", "package_id": None, "slug": slug, "name": "Test Engine",
        }
        args.update(overrides)
        with redirect_stdout(io.StringIO()):
            plaits_lab.init_command(SimpleNamespace(**args))
        return output

    def test_scaffold_stamps_the_author_into_license_and_source_headers(self) -> None:
        # The headers used to read "Copyright 2026 Contributor." regardless of
        # --author, so a scaffolded engine shipped a notice naming nobody.
        with tempfile.TemporaryDirectory() as temp_dir:
            package = self._scaffold(Path(temp_dir), "cloud-chamber")
            year = plaits_lab.current_year()
            self.assertIn(f"Copyright (c) {year} Ada Lovelace", (package / "LICENSE").read_text())
            for source in ("cloud-chamber_engine.h", "cloud-chamber_engine.cc"):
                text = (package / "src" / source).read_text(encoding="utf-8")
                self.assertIn(f"// Copyright {year} Ada Lovelace.", text)
                self.assertEqual(plaits_lab.source_spdx_id(text), "MIT")
            self.assertNotIn("Contributor.", (package / "src" / "cloud-chamber_engine.h").read_text())

    def test_every_allowed_license_scaffolds_and_validates(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            for spdx in sorted(plaits_lab.ALLOWED_LICENSES):
                with self.subTest(license=spdx):
                    slug = spdx.lower().replace(".", "-")
                    package = self._scaffold(Path(temp_dir), slug, license=spdx)
                    loaded = plaits_lab.load_package(str(package))
                    self.assertEqual(loaded["manifest"]["license"], spdx)
                    # The rendered LICENSE must be identifiable as what it claims,
                    # or the check below could never distinguish the four.
                    self.assertEqual(
                        plaits_lab.identify_license_text((package / "LICENSE").read_text()), spdx,
                    )

    def test_license_text_must_match_the_declared_license(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            package = self._scaffold(Path(temp_dir), "mismatch")
            manifest_path = package / "plaits-engine.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["license"] = "ISC"
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            with self.assertRaises(plaits_lab.PackageError) as context:
                plaits_lab.load_package(str(package))
            self.assertIn("MIT text but the manifest declares ISC", str(context.exception))

    def test_unrecognized_license_text_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            package = self._scaffold(Path(temp_dir), "copyleft")
            (package / "LICENSE").write_text(
                "GNU GENERAL PUBLIC LICENSE\nVersion 3\n\nCopyright (c) 2026 Ada\n", encoding="utf-8",
            )
            with self.assertRaises(plaits_lab.PackageError) as context:
                plaits_lab.load_package(str(package))
            self.assertIn("recognized license", str(context.exception))

    def test_license_without_a_copyright_holder_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            package = self._scaffold(Path(temp_dir), "holderless")
            (package / "LICENSE").write_text(
                f"{plaits_lab.LICENSE_TITLES['MIT']}\n\n{plaits_lab.MIT_BODY}", encoding="utf-8",
            )
            with self.assertRaises(plaits_lab.PackageError) as context:
                plaits_lab.load_package(str(package))
            self.assertIn("copyright line", str(context.exception))

    def test_source_files_must_carry_a_matching_spdx_tag(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            package = self._scaffold(Path(temp_dir), "untagged")
            source = package / "src" / "untagged_engine.cc"
            original = source.read_text(encoding="utf-8")

            source.write_text(original.replace("// SPDX-License-Identifier: MIT\n", ""), encoding="utf-8")
            with self.assertRaises(plaits_lab.PackageError) as context:
                plaits_lab.load_package(str(package))
            self.assertIn("no SPDX-License-Identifier", str(context.exception))

            source.write_text(original.replace("SPDX-License-Identifier: MIT",
                                               "SPDX-License-Identifier: GPL-3.0"), encoding="utf-8")
            with self.assertRaises(plaits_lab.PackageError) as context:
                plaits_lab.load_package(str(package))
            self.assertIn("declares SPDX-License-Identifier: GPL-3.0", str(context.exception))

    def test_fork_carries_both_copyright_notices(self) -> None:
        # A fork is a derivative work: the upstream notice has to survive in the
        # LICENSE and in the vendored source, alongside the contributor's own.
        with tempfile.TemporaryDirectory() as temp_dir:
            package = self._scaffold(
                Path(temp_dir), "va-fork", from_engine="virtual-analog", name=None,
            )
            license_text = (package / "LICENSE").read_text(encoding="utf-8")
            notices = plaits_lab.extract_copyright_notices(license_text)
            self.assertEqual(len(notices), 2)
            self.assertIn("Emilie Gillet", notices[0])
            self.assertIn("Ada Lovelace", notices[1])

            header = (package / "src" / "va-fork_engine.h").read_text(encoding="utf-8")
            self.assertIn("Modified from Virtual Analog", header)
            self.assertIn("Copyright 2016 Emilie Gillet", header)
            self.assertEqual(plaits_lab.source_spdx_id(header), "MIT")
            plaits_lab.load_package(str(package))

    def test_fork_may_not_relicense_away_from_upstream(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            with self.assertRaises(plaits_lab.PackageError) as context:
                self._scaffold(Path(temp_dir), "relicensed",
                               from_engine="virtual-analog", name=None, license="ISC")
            self.assertIn("must stay MIT", str(context.exception))

    def test_apache_2_is_not_silently_accepted(self) -> None:
        # Excluded deliberately (NOTICE propagation + patent-termination clause);
        # this pins the decision so a future edit is a conscious one.
        self.assertNotIn("Apache-2.0", plaits_lab.ALLOWED_LICENSES)
        apache = ("Apache License\nVersion 2.0\n\nCopyright (c) 2026 Ada\n"
                  "Licensed under the Apache License, Version 2.0\n")
        self.assertIsNone(plaits_lab.identify_license_text(apache))

    def test_dev_editor_ui_is_same_origin(self) -> None:
        html = (plaits_lab.SDK_DIR / "dev_editor.html").read_text(encoding="utf-8")
        # The built-in audition UI must talk to its OWN server via relative /v1
        # paths — never an absolute origin or the devServer cross-origin dance,
        # which is the whole reason it exists (no CORS/CSP/local-network issues).
        for endpoint in ("/v1/package", "/v1/render", "/v1/catalog"):
            self.assertIn(endpoint, html)
        self.assertNotIn("http://", html)
        self.assertNotIn("https://", html)
        self.assertNotIn("devServer", html)

    def test_live_audition_worklet_is_static_and_same_origin(self) -> None:
        # The AudioWorklet processor is served verbatim by the dev server and
        # registers under the name the page instantiates.
        worklet = (plaits_lab.SDK_DIR / "audition_worklet.js").read_text(encoding="utf-8")
        self.assertIn("registerProcessor('plaits-audition'", worklet)
        # The live-audition UI loads the wasm + worklet from its OWN origin.
        html = (plaits_lab.SDK_DIR / "dev_editor.html").read_text(encoding="utf-8")
        self.assertIn("/v1/audition.wasm", html)
        self.assertIn("/audition_worklet.js", html)

    def test_live_audition_envelope_surface_is_wired(self) -> None:
        # The Strike/Sustained-Plucked path: the harness must export set_env_mode,
        # implement the low-pass-gate decay, and the worklet must forward 'env'.
        self.assertIn("_set_env_mode", plaits_lab.WASM_EXPORTS)
        harness = (plaits_lab.SDK_DIR / "wasm_audition.cc").read_text(encoding="utf-8")
        self.assertIn("set_env_mode", harness)
        self.assertIn("ENV_PLUCKED", harness)
        worklet = (plaits_lab.SDK_DIR / "audition_worklet.js").read_text(encoding="utf-8")
        self.assertIn("'env'", worklet)

    def test_attenurandomizer_drift_audition_surface_is_wired(self) -> None:
        # The tuning page mirrors the firmware's continuous Drift behavior:
        # signed TIMBRE/MORPH depths select continuously evolving local/broad
        # voltages. Triggers do not clock or restart either process.
        self.assertIn("_set_randomizer_amounts", plaits_lab.WASM_EXPORTS)
        self.assertIn("_current_timbre", plaits_lab.WASM_EXPORTS)
        self.assertIn("_current_morph", plaits_lab.WASM_EXPORTS)
        harness = (plaits_lab.SDK_DIR / "wasm_audition.cc").read_text(encoding="utf-8")
        self.assertIn("set_randomizer_amounts", harness)
        self.assertIn("AdvanceChaosOrbit", harness)
        self.assertIn("ChaosOrbit", harness)
        self.assertNotIn("blocks_until_target", harness)
        self.assertIn("RandomizedParameter", harness)
        self.assertIn("kRandomizerDeadZone", harness)
        self.assertIn("RandomizerExcursion", harness)
        html = (plaits_lab.SDK_DIR / "dev_editor.html").read_text(encoding="utf-8")
        self.assertIn("Both directions remain bipolar", html)
        self.assertNotIn("Audition prototype", html)
        self.assertIn("RandomizerRateMultiplier", harness)
        self.assertIn("far_center_release", harness)
        self.assertIn("PLAITS_LAB_RANDOMIZER_TIMBRE_NEAR_SPAN", harness)
        self.assertIn("current_timbre", harness)
        worklet = (plaits_lab.SDK_DIR / "audition_worklet.js").read_text(encoding="utf-8")
        self.assertIn("'randomization'", worklet)
        self.assertIn("set_randomizer_amounts", worklet)
        self.assertIn("'modulation-state'", worklet)
        html = (plaits_lab.SDK_DIR / "dev_editor.html").read_text(encoding="utf-8")
        self.assertIn("randomizer-timbre", html)
        self.assertIn("randomizer-morph", html)
        self.assertIn("Engine now", html)
        self.assertIn("postRandomizerAmounts()", html)
        self.assertIn("await ensureLive()", html)
        self.assertNotIn("setInterval", html)
        self.assertNotIn("randomizer-auto", html)
        self.assertIn("RANDOMIZER_DEAD_ZONE", html)
        self.assertIn("'Near' : 'Far'", html)
        registry = plaits_lab.load_randomizer_profile_registry()
        catalog_ids = {
            engine["id"]
            for engine in plaits_lab.read_json(plaits_lab.CATALOG_PATH)["engines"]
        }
        self.assertEqual(catalog_ids, set(registry["models"]))
        self.assertEqual(
            sum(model["status"] == "tuned" for model in registry["models"].values()), 4)
        self.assertEqual(registry["models"]["virtual-analog"]["status"], "tuned")
        self.assertEqual(registry["models"]["fold"]["status"], "tuned")
        self.assertEqual(registry["models"]["vowel-fof"]["status"], "tuned")
        self.assertEqual(registry["models"]["granular-cloud"]["status"], "tuned")
        vowel = plaits_lab.randomizer_profile_for_catalog_id("vowel-fof")
        self.assertEqual(vowel["timbre"]["farSpan"], 0.52)
        self.assertEqual(vowel["morph"]["nearRate"], 0.000005)
        chords = plaits_lab.randomizer_profile_for_catalog_id("chords")
        self.assertEqual(chords["status"], "seeded")
        self.assertEqual(chords["morph"]["topology"], "stepped")
        self.assertEqual(
            plaits_lab.randomizer_profile_for_catalog_id("rulefield")["morph"]["archetype"],
            "event-rate")
        self.assertEqual(
            plaits_lab.randomizer_profile_for_catalog_id("ring-mod")["timbre"]["archetype"],
            "pitch-interval")
        self.assertEqual(
            plaits_lab.randomizer_profile_for_catalog_id("helix")["timbre"]["archetype"],
            "centered")
        self.assertEqual(
            plaits_lab.randomizer_profile_for_catalog_id("shakers")["morph"]["archetype"],
            "threshold-guarded")
        fallback = plaits_lab.randomizer_profile_for_catalog_id("not-profiled-yet")
        self.assertEqual(fallback["status"], "fallback")

    def test_compile_wasm_receives_resolved_randomizer_profile(self) -> None:
        package = plaits_lab.builtin_package("chords")
        captured: dict[str, object] = {}
        real_compiler = plaits_lab.wasm_compiler_path
        real_run = plaits_lab.subprocess.run
        plaits_lab.wasm_compiler_path = lambda: "emcc"

        def capture(command, **kwargs):
            captured["command"] = command
            return SimpleNamespace(returncode=0, stdout="", stderr="")

        plaits_lab.subprocess.run = capture
        try:
            plaits_lab.compile_wasm(package, Path(tempfile.gettempdir()) / "profiled.wasm")
        finally:
            plaits_lab.wasm_compiler_path = real_compiler
            plaits_lab.subprocess.run = real_run

        command = captured["command"]
        self.assertIn("-DPLAITS_LAB_RANDOMIZER_TIMBRE_NEAR_SPAN=0.18f", command)
        self.assertIn("-DPLAITS_LAB_RANDOMIZER_MORPH_FAR_SPAN=0.4f", command)

    def test_dev_session_accepts_builtin_catalog_id(self) -> None:
        session = plaits_lab.DevSession("chords", None)
        try:
            package = session.package()
        finally:
            session.close()
        self.assertEqual(package["manifest"]["id"], "mutable-instruments/chords")
        self.assertEqual(package["manifest"]["catalogId"], "chords")
        self.assertEqual(package["manifest"]["controls"][1]["label"], "Waveform")
        self.assertEqual(package["digest"], plaits_lab.builtin_engine("chords")[1]["digest"])

    def test_live_audition_stereo_surface_is_wired(self) -> None:
        # The audition must be able to drive parameters.stereo and read back
        # stereo_capable(), so a contributor can hear a stereo engine's L/R render.
        self.assertIn("_set_stereo", plaits_lab.WASM_EXPORTS)
        self.assertIn("_stereo_capable", plaits_lab.WASM_EXPORTS)
        harness = (plaits_lab.SDK_DIR / "wasm_audition.cc").read_text(encoding="utf-8")
        self.assertIn("g_params.stereo", harness)
        self.assertIn("stereo_capable", harness)
        worklet = (plaits_lab.SDK_DIR / "audition_worklet.js").read_text(encoding="utf-8")
        self.assertIn("'stereo'", worklet)
        self.assertIn("stereoCapable", worklet)
        # Stereo is a Monitor option that gates on stereo capability.
        html = (plaits_lab.SDK_DIR / "dev_editor.html").read_text(encoding="utf-8")
        self.assertIn("monitor-stereo-opt", html)

    def test_compile_wasm_without_emcc_reports_clearly(self) -> None:
        # Live audition is OPTIONAL: with no emcc on PATH the wasm build must fail
        # with a clear, actionable error rather than a raw traceback.
        package = plaits_lab.builtin_package("chords")
        real_which = plaits_lab.shutil.which
        plaits_lab.shutil.which = lambda name: None if name == "emcc" else real_which(name)
        try:
            self.assertIsNone(plaits_lab.wasm_compiler_path())
            with self.assertRaises(plaits_lab.PackageError) as ctx:
                plaits_lab.compile_wasm(package, Path(tempfile.gettempdir()) / "unbuilt.wasm")
            self.assertIn("emcc", str(ctx.exception))
        finally:
            plaits_lab.shutil.which = real_which

    def test_renderer_and_wasm_share_translation_units(self) -> None:
        # Both native and wasm builds must compile the SAME de-duplicated source
        # set (only the entry harness differs) — the invariant behind the shared
        # engine_translation_units() helper.
        package = plaits_lab.builtin_package("chords")
        renderer_entry = plaits_lab.SDK_DIR / "render_model.cc"
        wasm_entry = plaits_lab.SDK_DIR / "wasm_audition.cc"
        renderer_units = plaits_lab.engine_translation_units(package, renderer_entry)
        wasm_units = plaits_lab.engine_translation_units(package, wasm_entry)
        self.assertEqual(renderer_units[1:], wasm_units[1:])
        self.assertEqual(len(renderer_units), len(set(renderer_units)))

    @unittest.skipUnless(shutil.which("emcc"), "emscripten (emcc) required")
    def test_compile_wasm_builds_standalone_module(self) -> None:
        package = plaits_lab.builtin_package("chords")
        with tempfile.TemporaryDirectory() as temp_dir:
            out = Path(temp_dir) / "audition.wasm"
            plaits_lab.compile_wasm(package, out)
            data = out.read_bytes()
            self.assertTrue(data.startswith(b"\x00asm"))  # wasm magic
            self.assertGreater(len(data), 1000)

    @unittest.skipUnless(shutil.which("c++") or shutil.which("g++"), "host C++ compiler required")
    def test_from_scratch_engine_can_link_shared_chord_bank(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            package_dir = Path(temp_dir) / "chord-probe"
            with redirect_stdout(io.StringIO()):
                plaits_lab.init_command(SimpleNamespace(
                    output=str(package_dir), from_engine="blank",
                    author="Test Author", package_id="test-author/chord-probe",
                    slug="chord-probe", name="Chord Probe",
                ))
            (package_dir / "src" / "chord-probe_engine.h").write_text(
                CHORD_PROBE_HEADER, encoding="utf-8")
            (package_dir / "src" / "chord-probe_engine.cc").write_text(
                CHORD_PROBE_IMPL, encoding="utf-8")
            manifest_path = package_dir / "plaits-engine.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["sharedModules"] = ["chord-bank"]
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

            package = plaits_lab.load_package(str(package_dir))
            renderer = plaits_lab.host_binary(Path(temp_dir), "render-model")
            plaits_lab.compile_renderer(package, renderer, None)
            self.assertTrue(renderer.exists())

    @unittest.skipUnless(shutil.which("c++") or shutil.which("g++"), "host C++ compiler required")
    def test_reference_shared_module_consumers_link(self) -> None:
        # Every built-in that declares a shared module must still link when its
        # module .cc are resolved from the registry instead of source.files.
        with tempfile.TemporaryDirectory() as temp_dir:
            for engine_id in (
                "chords", "chiptune", "string-machine",
                "inharmonic-string", "modal-resonator", "dx7-bank-a",
            ):
                with self.subTest(engine=engine_id):
                    package = plaits_lab.builtin_package(engine_id)
                    self.assertTrue(package["manifest"]["sharedModules"])
                    renderer = plaits_lab.host_binary(Path(temp_dir), f"reference-{engine_id}")
                    plaits_lab.compile_renderer(package, renderer, None)
                    self.assertTrue(renderer.exists())

    @unittest.skipUnless(shutil.which("c++") or shutil.which("g++"), "host C++ compiler required")
    def test_six_op_reference_loads_factory_bank_and_renders_audio(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            package = plaits_lab.builtin_package("dx7-bank-a")
            self.assertEqual(package["user_data_bank"], 0)
            renderer = plaits_lab.host_binary(Path(temp_dir), "reference-dx7")
            plaits_lab.compile_renderer(package, renderer, None)
            scenario = package["scenarios"][0]
            output = Path(temp_dir) / "dx7.wav"
            elapsed = plaits_lab.run_scenario(package, renderer, scenario, output)
            analysis = plaits_lab.analyze_wav(
                output, scenario["durationSeconds"], elapsed)
            self.assertGreater(analysis["peak"], 0.01)

    def _community_package(self, temp_dir: str, slug: str) -> dict:
        pkg_dir = Path(temp_dir) / slug
        with redirect_stdout(io.StringIO()):
            plaits_lab.init_command(SimpleNamespace(
                output=str(pkg_dir), from_engine="blank", author="T",
                package_id=f"test-author/{slug}", slug=slug, name=slug.title()))
        return plaits_lab.load_package(str(pkg_dir))

    def _capture_docker_run(self, fn) -> tuple[list[str], str]:
        """Run fn() with docker present and subprocess.run stubbed; return the
        command docker was handed plus anything printed."""
        captured: dict[str, list[str]] = {}
        real_run, real_which = plaits_lab.subprocess.run, plaits_lab.shutil.which
        plaits_lab.shutil.which = (
            lambda name: "/usr/bin/docker" if name == "docker" else real_which(name))
        plaits_lab.subprocess.run = lambda cmd, **kw: (
            captured.__setitem__("cmd", cmd) or SimpleNamespace(returncode=0, stdout="", stderr=""))
        out = io.StringIO()
        try:
            with redirect_stdout(out):
                fn()
        finally:
            plaits_lab.subprocess.run, plaits_lab.shutil.which = real_run, real_which
        return captured.get("cmd", []), out.getvalue()

    def test_full_check_delegates_to_the_builder_image_without_host_sanitizers(self) -> None:
        # MinGW-w64 compiles the sanitizer flags and then cannot link them, so on
        # Windows the submission gate has to run in the image instead of failing.
        with tempfile.TemporaryDirectory() as temp_dir:
            package = self._community_package(temp_dir, "gated")
            args = SimpleNamespace(package=str(package["directory"]), compiler=None,
                                   no_compile=False, full=True, arm=False,
                                   toolchain="/nonexistent-arm",
                                   docker_image="img:test", native=False)
            real_probe = plaits_lab.host_sanitizers_available
            plaits_lab.host_sanitizers_available = lambda _compiler: False
            try:
                cmd, printed = self._capture_docker_run(
                    lambda: plaits_lab.check_command(args))
            finally:
                plaits_lab.host_sanitizers_available = real_probe

        self.assertEqual(cmd[:3], ["/usr/bin/docker", "run", "--rm"])
        self.assertIn("--platform", cmd)
        self.assertEqual(cmd[cmd.index("--platform") + 1], "linux/amd64")
        self.assertEqual(cmd[-4:], ["check", "/contributor", "--full", "--native"])
        # Both mounts read-only: the container must not write into the checkout.
        self.assertIn(f"{package['repo_root']}:/workspace:ro", cmd)
        self.assertIn(f"{package['directory']}:/contributor:ro", cmd)
        self.assertIn("cannot link the sanitizers", printed)

    def test_submit_delegates_with_a_writable_output_mount(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            package = self._community_package(temp_dir, "bundled")
            out_zip = Path(temp_dir) / "dist" / "bundled.zip"
            # bundle_only on the OUTER call keeps this test off the network; the
            # inner container's own --bundle-only is asserted below and is not
            # the same thing.
            args = SimpleNamespace(package=str(package["directory"]), compiler=None,
                                   output=str(out_zip), docker_image="img:test",
                                   native=False, bundle_only=True)
            real_probe = plaits_lab.host_sanitizers_available
            plaits_lab.host_sanitizers_available = lambda _compiler: False
            try:
                cmd, _ = self._capture_docker_run(lambda: plaits_lab.submit_command(args))
            finally:
                plaits_lab.host_sanitizers_available = real_probe

        # The zip has to come back out, so /output is the one writable mount and
        # the container is told to write there under the caller's chosen name.
        # The inner run BUILDS ONLY: it has no credentials and no terminal to
        # confirm at, and if it reached the upload the package would be
        # submitted twice — once from the container, once from out here.
        self.assertIn(f"{out_zip.resolve().parent}:/output", cmd)
        self.assertEqual(
            cmd[-6:],
            ["submit", "/contributor", "--output", "/output/bundled.zip",
             "--native", "--bundle-only"])

    def test_native_never_delegates_to_docker(self) -> None:
        # --native is what the SDK passes to ITSELF inside the container. If it
        # delegated, the container would try to run docker in docker forever.
        with tempfile.TemporaryDirectory() as temp_dir:
            package = self._community_package(temp_dir, "inner")
            args = SimpleNamespace(package=str(package["directory"]), compiler=None,
                                   no_compile=True, full=True, arm=False,
                                   toolchain="/nonexistent-arm",
                                   docker_image="img:test", native=True)
            real_probe = plaits_lab.host_sanitizers_available
            plaits_lab.host_sanitizers_available = lambda _compiler: False
            try:
                cmd, _ = self._capture_docker_run(lambda: plaits_lab.check_command(args))
            finally:
                plaits_lab.host_sanitizers_available = real_probe
        self.assertEqual(cmd, [])  # docker was never invoked

    def test_sanitizer_probe_tests_the_flags_the_real_build_uses(self) -> None:
        # A probe that checked different flags than compile_renderer injects could
        # green-light a toolchain that then fails the actual sanitized build.
        source = Path(plaits_lab.__file__).read_text(encoding="utf-8")
        self.assertIn("command[4:4] = SANITIZER_FLAGS", source)
        self.assertEqual(
            plaits_lab.SANITIZER_FLAGS,
            ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])

    def test_scenarios_run_with_leak_detection_off(self) -> None:
        # LSan exists on Linux but not macOS, so leaving it on makes the same
        # package pass check --full on one and fail on the other — including in
        # the builder image Windows delegates to. It can only ever fire on the
        # SDK's own harness anyway: contributor source cannot allocate (below).
        self.assertEqual(plaits_lab.SANITIZER_RUNTIME_ENV.get("ASAN_OPTIONS"),
                         "detect_leaks=0")
        captured: dict[str, dict] = {}
        real_run = plaits_lab.subprocess.run
        plaits_lab.subprocess.run = lambda cmd, **kw: (
            captured.__setitem__("env", kw.get("env") or {})
            or SimpleNamespace(returncode=0, stdout="", stderr=""))
        try:
            plaits_lab.run_scenario(
                {"manifest": {"postProcessing": {"outGain": 1.0, "auxGain": 1.0}}},
                Path("renderer"),
                {"id": "hero", "durationSeconds": 1, "note": 48, "triggerHz": 0,
                 "controls": {k: [0.0, 1.0] for k in
                              ("harmonics", "timbre", "morph", "macro")}},
                Path("out.wav"))
        finally:
            plaits_lab.subprocess.run = real_run
        self.assertEqual(captured["env"].get("ASAN_OPTIONS"), "detect_leaks=0")
        # and the rest of the environment survives, or the compiler/PATH is lost
        self.assertIn("PATH", {k.upper(): v for k, v in captured["env"].items()})

    def test_scenario_gain_can_be_overridden_for_linear_measurement(self) -> None:
        captured: dict[str, list[str]] = {}
        real_run = plaits_lab.subprocess.run
        plaits_lab.subprocess.run = lambda cmd, **kw: (
            captured.__setitem__("cmd", cmd)
            or SimpleNamespace(returncode=0, stdout="", stderr=""))
        try:
            plaits_lab.run_scenario(
                {"manifest": {"postProcessing": {"outGain": -3.8, "auxGain": 2.4}}},
                Path("renderer"),
                {"id": "ab", "durationSeconds": 1, "note": 48, "triggerHz": 0,
                 "controls": {k: [0.0, 1.0] for k in
                              ("harmonics", "timbre", "morph", "macro")}},
                Path("out.wav"), out_gain=1.0, aux_gain=1.0)
        finally:
            plaits_lab.subprocess.run = real_run
        self.assertEqual(captured["cmd"][-2:], ["1.0", "1.0"])

    def test_contributor_source_cannot_allocate(self) -> None:
        # The premise that makes disabling LSan lossless: an engine that heap
        # allocates is rejected by policy long before it is ever compiled.
        for snippet in ("void Render() { void* p = malloc(4); }",
                        "void Render() { int* p = new int; }",
                        "void Render() { free(ptr); }"):
            with self.subTest(snippet=snippet):
                with tempfile.TemporaryDirectory() as temp_dir:
                    source = Path(temp_dir) / "engine.cc"
                    source.write_text(snippet, encoding="utf-8")
                    with self.assertRaises(plaits_lab.PackageError):
                        plaits_lab.validate_community_source([source])

    def test_output_is_utf8_even_when_redirected(self) -> None:
        # The CLI prints ✓. On Windows a redirected stdout falls back to cp1252,
        # which has no ✓, so every command died with UnicodeEncodeError the
        # moment output was piped or captured to a log.
        buffer = io.BytesIO()
        stream = io.TextIOWrapper(buffer, encoding="cp1252", newline="")
        real_stdout, real_stderr = sys.stdout, sys.stderr
        sys.stdout = sys.stderr = stream
        try:
            plaits_lab.use_utf8_output()
            print("✓ package ok")
            stream.flush()
        finally:
            sys.stdout, sys.stderr = real_stdout, real_stderr
        self.assertEqual(stream.encoding.lower().replace("-", ""), "utf8")
        self.assertIn("✓".encode("utf-8"), buffer.getvalue())

    def test_content_digest_hashes_in_posix_relative_order(self) -> None:
        # The digest is the package's identity, so it must not depend on the
        # host's path ordering. Sorting Path OBJECTS would break that: on
        # Windows they compare case-folded and backslash-separated, so
        # "README.md" lands after "plaits-engine.json" and a nested "dsp/x.cc"
        # moves relative to "dsp-extra.cc" — a different digest for the same
        # bytes. These names are chosen to expose both differences.
        names = [
            "README.md", "LICENSE", "plaits-engine.json",
            "dsp/filter.cc", "dsp-extra.cc",
        ]
        with tempfile.TemporaryDirectory() as temp_dir:
            package_dir = Path(temp_dir) / "package"
            for name in names:
                path = package_dir / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(f"contents of {name}\n".encode("utf-8"))

            expected = hashlib.sha256()
            for relative in sorted(names):  # plain strings: POSIX order, everywhere
                expected.update(relative.encode("utf-8"))
                expected.update(b"\0")
                expected.update((package_dir / relative).read_bytes())

            self.assertEqual(
                plaits_lab.package_content_digest(package_dir),
                "sha256:" + expected.hexdigest(),
            )


if __name__ == "__main__":
    unittest.main()
