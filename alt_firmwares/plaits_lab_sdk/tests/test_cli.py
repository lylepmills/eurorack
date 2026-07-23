from __future__ import annotations

import importlib.util
import io
import json
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


CHORD_PROBE_HEADER = """\
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

    def test_authoritative_catalog_exposes_every_forkable_model(self) -> None:
        catalog, public = plaits_lab.load_builtin_catalog()
        self.assertEqual(len(catalog), 39)
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
        with tempfile.TemporaryDirectory() as temp_dir:
            package = Path(temp_dir) / "bright-wave"
            plaits_lab.init_command(SimpleNamespace(
                output=str(package), from_engine="blank", author="Test Author",
                package_id="test-author/bright-wave", slug="bright-wave", name="Bright Wave",
            ))
            loaded = plaits_lab.load_package(str(package))
            self.assertEqual(loaded["manifest"]["packageType"], "community")

            bundle = Path(temp_dir) / "bright-wave.zip"
            plaits_lab.submit_command(SimpleNamespace(
                package=str(package), output=str(bundle), compiler=None,
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

    def test_dev_contributor_url_preserves_editor_path(self) -> None:
        # The bug: the full page path was dropped for a bare /contribute.
        self.assertEqual(
            plaits_lab.contributor_url_for(
                "https://rubato.audio/plaits-palette/contribute", "http://127.0.0.1:4179"),
            "https://rubato.audio/plaits-palette/contribute"
            "?devServer=http%3A%2F%2F127.0.0.1%3A4179",
        )
        # A bare origin falls back to the contributor route, not top-level /contribute.
        self.assertEqual(
            plaits_lab.contributor_url_for("http://localhost:4321", "http://127.0.0.1:4179"),
            "http://localhost:4321/plaits-palette/contribute"
            "?devServer=http%3A%2F%2F127.0.0.1%3A4179",
        )

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
            renderer = Path(temp_dir) / "render-model"
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
                    renderer = Path(temp_dir) / f"reference-{engine_id}"
                    plaits_lab.compile_renderer(package, renderer, None)
                    self.assertTrue(renderer.exists())

    @unittest.skipUnless(shutil.which("c++") or shutil.which("g++"), "host C++ compiler required")
    def test_six_op_reference_survives_null_user_data(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            package = plaits_lab.builtin_package("dx7-bank-a")
            renderer = Path(temp_dir) / "reference-dx7"
            plaits_lab.compile_renderer(package, renderer, None)
            scenario = package["scenarios"][0]
            output = Path(temp_dir) / "dx7.wav"
            # Before the LoadUserData null-guard this render SIGSEGVs.
            plaits_lab.run_scenario(package, renderer, scenario, output)
            self.assertTrue(output.exists())


if __name__ == "__main__":
    unittest.main()
