// Per-engine render-cost benchmark for the Plaits models.
//
// Times each engine's Engine::Render for a full audio block in mono
// (parameters.stereo = false) and stereo (true), steady state, and prints
//   <catalog-id> <mono_ns> <stereo_ns>
// (or "<id> CRASHED"). Each engine runs in a forked child so one bad
// instantiation cannot abort the sweep. Downstream tooling computes the
// stereo/mono ratio and normalizes to the heaviest engine, and re-runs this
// binary compiled -funroll-loops vs -fno-unroll-loops to show the unroll cost.
//
// The absolute nanoseconds are host x86 numbers, NOT the module's cycle budget
// — use the RATIOS (stereo/mono, %-of-heaviest, unroll on/off), which are
// architecture-robust. Build/run with `make cpu-bench` (see plaits/test/makefile).
#include <cstdio>
#include <cstring>
#include <chrono>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <xmmintrin.h>
#include <pmmintrin.h>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_crossfade_engine.h"
#include "plaits/dsp/engine/virtual_analog_dual_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine2/attractor_engine.h"
#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/gendy_engine.h"
#include "plaits/dsp/engine2/glisson_engine.h"
#include "plaits/dsp/engine2/lockstep_engine.h"
#include "plaits/dsp/engine2/loopback_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/phase_flock_engine.h"
#include "plaits/dsp/engine2/phase_weave_engine.h"
#include "plaits/dsp/engine2/pulsar_engine.h"
#include "plaits/dsp/engine2/reed_pipe_engine.h"
#include "plaits/dsp/engine2/rulefield_engine.h"
#include "plaits/dsp/engine2/scanned_engine.h"
#include "plaits/dsp/engine2/sideband_engine.h"
#include "plaits/dsp/engine2/spectral_spiral_engine.h"
#include "plaits/dsp/engine2/bowed_engine.h"
#include "plaits/dsp/engine2/question_mark_engine.h"
#include "plaits/dsp/engine2/fluted_engine.h"
#include "plaits/dsp/engine2/formant_speech_engine.h"
#include "plaits/dsp/engine2/wave_paraphonic_engine.h"
#include "plaits/dsp/engine2/wave_scan_engine.h"
#include "plaits/dsp/engine2/cymbal_engine.h"
#include "plaits/dsp/engine2/snare_engine.h"
#include "plaits/dsp/engine2/kick_engine.h"
#include "plaits/dsp/engine2/lpc_speech_engine.h"
#include "plaits/dsp/engine2/struck_drum_engine.h"
#include "plaits/dsp/engine2/struck_bell_engine.h"
#include "plaits/dsp/engine2/blown_engine.h"
#include "plaits/dsp/engine2/plucked_engine.h"
#include "plaits/dsp/engine2/vosim_engine.h"
#include "plaits/dsp/engine2/harmonics_engine.h"
#include "plaits/dsp/engine2/vowel_engine.h"
#include "plaits/dsp/engine2/saw_swarm_engine.h"
#include "plaits/dsp/engine2/saw_square_engine.h"
#include "plaits/dsp/engine2/particle_burst_engine.h"
#include "plaits/dsp/engine2/noise_bank_engine.h"
#include "plaits/dsp/engine2/morph_engine.h"
#include "plaits/dsp/engine2/granular_cloud_engine.h"
#include "plaits/dsp/engine2/dual_sync_engine.h"
#include "plaits/dsp/engine2/buzz_engine.h"
#include "plaits/dsp/engine2/fold_engine.h"
#include "plaits/dsp/engine2/csaw_engine.h"
#include "plaits/dsp/engine2/ring_mod_engine.h"
#include "plaits/dsp/engine2/sub_oscillator_engine.h"
#include "plaits/dsp/engine2/toy_engine.h"
#include "plaits/dsp/engine2/digital_modulation_engine.h"
#include "plaits/dsp/engine2/saw_comb_engine.h"
#include "plaits/dsp/engine2/vowel_fof_engine.h"
#include "plaits/dsp/engine2/raw_fm_engine.h"
#include "plaits/dsp/engine2/triple_engine.h"
#include "plaits/dsp/engine2/bytebeat_engine.h"
#include "plaits/dsp/engine2/diatonic_chord_engine.h"
#include "plaits/dsp/engine2/scale_stack_engine.h"
#include "plaits/dsp/engine2/wavetable_chord_engine.h"
#include "plaits/dsp/engine2/wavetable_scale_stack_engine.h"
#include "plaits/dsp/engine2/shakers_engine.h"
#include "plaits/dsp/engine2/brass_engine.h"
#include "plaits/dsp/engine2/clap_engine.h"
#include "plaits/dsp/engine2/analog_percussion_engine.h"
#include "plaits/dsp/engine2/freshets_formant_engine.h"
#include "plaits/dsp/engine2/z_filter_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/tapfield_engine.h"
#include "plaits/dsp/engine2/undertow_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"

using namespace plaits;
using namespace stmlib;
static char ram[128 * 1024];
const size_t B = 24;
const int N = 120000;

template <typename E>
void bench_one(const char* name) {
  // Flush denormals to zero (both operands and results). The Cortex-M4 FPU
  // does this in hardware; on x86 a denormal-producing engine renders ~100x
  // slower, which otherwise reads as a hang. Also arms a hard timeout so a
  // genuinely stuck engine can't wedge the sweep.
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
  alarm(8);
  BufferAllocator allocator(ram, sizeof(ram));
  E e;
  e.Init(&allocator);
  EngineParameters p;
  p.trigger = TRIGGER_UNPATCHED;
  p.note = 36.0f; p.timbre = 0.5f; p.morph = 0.5f; p.harmonics = 0.5f;
  p.accent = 0.8f; p.macro = 0.5f; p.chord_set_option = 0;
  float out[B], aux[B]; bool env;
  auto run = [&](bool stereo, int iters) {
    p.stereo = stereo;
    for (int i = 0; i < iters; ++i) e.Render(p, out, aux, B, &env);
  };
  run(false, 3000); run(true, 3000);
  auto t0 = std::chrono::high_resolution_clock::now();
  run(false, N);
  auto t1 = std::chrono::high_resolution_clock::now();
  run(true, N);
  auto t2 = std::chrono::high_resolution_clock::now();
  double mono = std::chrono::duration<double, std::nano>(t1 - t0).count() / N;
  double st = std::chrono::duration<double, std::nano>(t2 - t1).count() / N;
  printf("%-20s %10.1f %10.1f\n", name, mono, st);
}

template <typename E>
void bench(const char* name) {
  pid_t pid = fork();
  if (pid == 0) { bench_one<E>(name); fflush(stdout); _exit(0); }
  int status = 0;
  waitpid(pid, &status, 0);
  if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
    const char* why = (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM)
        ? "TIMEOUT" : "CRASHED";
    printf("%-20s %10s %10s\n", name, why, why);
    fflush(stdout);
  }
}

int main() {
  // Stock Mutable Instruments models.
  bench<VirtualAnalogEngine>("virtual-analog");
  bench<VirtualAnalogDualEngine>("virtual-analog-dual");
  bench<VirtualAnalogCrossfadeEngine>("virtual-analog-crossfade");
  bench<WaveshapingEngine>("waveshaping");
  bench<FMEngine>("two-op-fm");
  bench<GrainEngine>("granular-formant");
  bench<AdditiveEngine>("harmonic");
  bench<WavetableEngine>("wavetable");
  bench<ChordEngine>("chords");
  bench<SpeechEngine>("speech");
  bench<SwarmEngine>("swarm");
  bench<NoiseEngine>("filtered-noise");
  bench<ParticleEngine>("particle-noise");
  bench<StringEngine>("inharmonic-string");
  bench<ModalEngine>("modal-resonator");
  bench<BassDrumEngine>("analog-bass-drum");
  bench<SnareDrumEngine>("analog-snare");
  bench<HiHatEngine>("analog-hi-hat");
  bench<VirtualAnalogVCFEngine>("virtual-analog-vcf");
  bench<PhaseDistortionEngine>("phase-distortion");
  bench<WaveTerrainEngine>("wave-terrain");
  bench<StringMachineEngine>("string-machine");
  // wavetable needs a wavetable loaded via LoadUserData(); chiptune's
  // arpeggiator wedges under this generic harness with flush-to-zero on. Both
  // need a bespoke setup — they print CRASHED / TIMEOUT here rather than wedging
  // the sweep. (chiptune measured ~709/785 ns mono/stereo in an ad-hoc run.)
  bench<ChiptuneEngine>("chiptune");
  // Rubato Lab models.
  bench<FormantSpeechEngine>("formant-speech");
  bench<LPCSpeechEngine>("lpc-speech");
  bench<GlissonEngine>("glisson");
  bench<GendyEngine>("gendy");
  bench<ScannedEngine>("scanned");
  bench<PulsarEngine>("pulsar");
  bench<BowedEngine>("bowed");
  bench<QuestionMarkEngine>("question-mark");
  bench<FlutedEngine>("fluted");
  bench<WaveParaphonicEngine>("wave-paraphonic");
  bench<WaveScanEngine>("wave-scan");
  bench<CymbalEngine>("cymbal");
  bench<SnareEngine>("snare");
  bench<KickEngine>("kick");
  bench<StruckDrumEngine>("struck-drum");
  bench<StruckBellEngine>("struck-bell");
  bench<BlownEngine>("blown");
  bench<PluckedEngine>("plucked");
  bench<VosimEngine>("vosim");
  bench<HarmonicsEngine>("harmonics");
  bench<VowelEngine>("vowel");
  bench<SawSwarmEngine>("saw-swarm");
  bench<SawSquareEngine>("saw-square");
  bench<ParticleBurstEngine>("particle-burst");
  bench<NoiseBankEngine>("noise-bank");
  bench<MorphEngine>("morph");
  bench<GranularCloudEngine>("granular-cloud");
  bench<DualSyncEngine>("dual-sync");
  bench<BuzzEngine>("buzz");
  bench<FoldEngine>("fold");
  bench<CSawEngine>("csaw");
  bench<RingModEngine>("ring-mod");
  bench<SubOscillatorEngine>("sub-oscillator");
  bench<ToyEngine>("toy");
  bench<DigitalModulationEngine>("digital-modulation");
  bench<SawCombEngine>("saw-comb");
  bench<VowelFofEngine>("vowel-fof");
  bench<RawFmEngine>("raw-fm");
  bench<TripleEngine>("triple");
  bench<BytebeatEngine>("bytebeat");
  bench<DiatonicChordEngine>("diatonic-chord");
  bench<ScaleStackEngine>("scale-stack");
  bench<WavetableChordEngine>("wavetable-chord");
  bench<WavetableScaleStackEngine>("wavetable-scale-stack");
  bench<ShakersEngine>("shakers");
  bench<BrassEngine>("brass");
  bench<ClapEngine>("clap");
  bench<AnalogPercussionEngine>("analog-percussion");
  bench<FreshetsFormantEngine>("freshets-formant");
  bench<ZFilterEngine>("z-filter");
  bench<LoopbackEngine>("loopback");
  bench<LockstepEngine>("lockstep");
  bench<TapfieldEngine>("tapfield");
  bench<PhaseWeaveEngine>("phase-weave");
  bench<SidebandEngine>("sideband-bank");
  bench<AttractorEngine>("attractor");
  bench<UndertowEngine>("undertow");
  bench<ReedPipeEngine>("reed-pipe");
  bench<PhaseFlockEngine>("phase-flock");
  bench<RulefieldEngine>("rulefield");
  bench<SpectralSpiralEngine>("spectral-spiral");
  return 0;
}
