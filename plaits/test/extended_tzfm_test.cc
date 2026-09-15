// Copyright 2026 Lyle Mills. SPDX-License-Identifier: MIT
// Regression matrix for Palette's signed-frequency expansion. The same source
// is built with the target option off and on to verify policy isolation.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdint.h>
#include <vector>
#include "plaits/resources.h"
#include "stmlib/utils/random.h"
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
#include "plaits/dsp/engine2/acid_engine.h"
#include "plaits/dsp/engine2/analog_percussion_engine.h"
#include "plaits/dsp/engine2/attractor_engine.h"
#include "plaits/dsp/engine2/blown_engine.h"
#include "plaits/dsp/engine2/bowed_engine.h"
#include "plaits/dsp/engine2/brass_engine.h"
#include "plaits/dsp/engine2/bubbletime_engine.h"
#include "plaits/dsp/engine2/buzz_engine.h"
#include "plaits/dsp/engine2/bytebeat_engine.h"
#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/circuit_zaps_engine.h"
#include "plaits/dsp/engine2/clap_engine.h"
#include "plaits/dsp/engine2/csaw_engine.h"
#include "plaits/dsp/engine2/cymbal_engine.h"
#include "plaits/dsp/engine2/diatonic_chord_engine.h"
#include "plaits/dsp/engine2/digital_modulation_engine.h"
#include "plaits/dsp/engine2/dual_sync_engine.h"
#include "plaits/dsp/engine2/fluted_engine.h"
#include "plaits/dsp/engine2/fold_engine.h"
#include "plaits/dsp/engine2/formant_speech_engine.h"
#include "plaits/dsp/engine2/freshets_formant_engine.h"
#include "plaits/dsp/engine2/gendy_engine.h"
#include "plaits/dsp/engine2/glisson_engine.h"
#include "plaits/dsp/engine2/granular_cloud_engine.h"
#include "plaits/dsp/engine2/harmonics_engine.h"
#include "plaits/dsp/engine2/helix_engine.h"
#include "plaits/dsp/engine2/kick_engine.h"
#include "plaits/dsp/engine2/lockstep_engine.h"
#include "plaits/dsp/engine2/loopback_engine.h"
#include "plaits/dsp/engine2/lpc_speech_engine.h"
#include "plaits/dsp/engine2/metalwork_engine.h"
#include "plaits/dsp/engine2/morph_engine.h"
#include "plaits/dsp/engine2/natural_speech_engine.h"
#include "plaits/dsp/engine2/noise_bank_engine.h"
#include "plaits/dsp/engine2/particle_burst_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/phase_flock_engine.h"
#include "plaits/dsp/engine2/phase_weave_engine.h"
#include "plaits/dsp/engine2/plucked_engine.h"
#include "plaits/dsp/engine2/pulsar_engine.h"
#include "plaits/dsp/engine2/question_mark_engine.h"
#include "plaits/dsp/engine2/raw_fm_engine.h"
#include "plaits/dsp/engine2/reed_pipe_engine.h"
#include "plaits/dsp/engine2/ring_mod_engine.h"
#include "plaits/dsp/engine2/rulefield_engine.h"
#include "plaits/dsp/engine2/saw_comb_engine.h"
#include "plaits/dsp/engine2/saw_square_engine.h"
#include "plaits/dsp/engine2/saw_swarm_engine.h"
#include "plaits/dsp/engine2/scale_stack_engine.h"
#include "plaits/dsp/engine2/scanned_engine.h"
#include "plaits/dsp/engine2/shakers_engine.h"
#include "plaits/dsp/engine2/sideband_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/skins_engine.h"
#include "plaits/dsp/engine2/snare_engine.h"
#include "plaits/dsp/engine2/spectral_spiral_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/struck_bell_engine.h"
#include "plaits/dsp/engine2/struck_drum_engine.h"
#include "plaits/dsp/engine2/sub_oscillator_engine.h"
#include "plaits/dsp/engine2/tapfield_engine.h"
#include "plaits/dsp/engine2/toy_engine.h"
#include "plaits/dsp/engine2/triple_engine.h"
#include "plaits/dsp/engine2/undertow_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/vosim_engine.h"
#include "plaits/dsp/engine2/vowel_engine.h"
#include "plaits/dsp/engine2/vowel_fof_engine.h"
#include "plaits/dsp/engine2/wave_paraphonic_engine.h"
#include "plaits/dsp/engine2/wave_scan_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"
#include "plaits/dsp/engine2/wavetable_chord_engine.h"
#include "plaits/dsp/engine2/wavetable_scale_stack_engine.h"
#include "plaits/dsp/engine2/z_filter_engine.h"
#include "plaits/dsp/engine2/zxphase48k_engine.h"
#include "plaits/dsp/engine2/zxpulse48k_engine.h"
using namespace plaits;
static int failures = 0;
static int tzfm_count = 0, fast_count = 0;
static void Check(bool ok, const char* id, const char* what) {
  if (!ok) { fprintf(stderr, "FAIL %s: %s\n", id, what); ++failures; }
}
template<class T> std::vector<float> Render(const char* id, int bank,
    float control, bool stereo, bool triggered, int mode, float pitch = 48.f) {
  alignas(T) unsigned char storage[sizeof(T)] = {};
  std::vector<unsigned char> ram(16 * 1024, 0);
  stmlib::BufferAllocator allocator(&ram[0], ram.size());
  T* engine = new(storage) T;
  stmlib::Random::Seed(0x12345678);
  engine->Init(&allocator);
  engine->LoadUserData(bank >= 0 ? fm_patches_table[bank] : NULL);
  engine->Reset();
  EngineParameters p = {};
  p.note = pitch; p.harmonics = control; p.timbre = 1.f - control;
  p.morph = control; p.macro = control; p.accent = .8f; p.stereo = stereo;
  const float base = NoteToFrequency(p.note);
  std::vector<float> result;
  for (int block = 0; block < 1024; ++block) {
    float offsets[12], left[12] = {}, right[12] = {};
    for (int i = 0; i < 12; ++i) {
      const int n = block * 12 + i;
      // modes: baseline, exact stop, negative-only, repeated zero crossings,
      // high-depth modulation. Negative-only MUST differ from the stop case.
      offsets[i] = mode == 7 ? base * (stmlib::SemitonesToRatio(36.f * sinf(n * .137f)) - 1.f) : mode == 6 ? fabsf(base * 3.f * sinf(n * .137f)) - base : mode == 1 ? -base : mode == 2 ?
          -base * (1.8f + .4f * sinf(n * .113f)) :
          mode == 3 ? -base + base * 3.f * sinf(n * .137f) :
          .45f * sinf(n * .673f);
    }
#if PLAITS_BUILD_EXTENDED_TZFM
    p.frequency_offset_is_linear = mode != 7;
#endif
    p.frequency_offset = mode && (mode != 5 || (block % 8) < 4) ? offsets : NULL;
    p.trigger = triggered ? (block == 0 ? TRIGGER_RISING_EDGE | TRIGGER_HIGH :
        block < 192 ? TRIGGER_HIGH : 0) : TRIGGER_UNPATCHED;
    bool enveloped = false;
    engine->Render(p, left, right, 12, &enveloped);
    for (int i = 0; i < 12; ++i) {
      if (!std::isfinite(left[i]) || !std::isfinite(right[i]) ||
          fabsf(left[i]) > 100.f || fabsf(right[i]) > 100.f) {
        fprintf(stderr, "FAIL %s: invalid output mode=%d control=%.2f stereo=%d trigger=%d at=%d (%g,%g)\n",
            id, mode, control, stereo, triggered, block * 12 + i, left[i], right[i]);
        ++failures; engine->T::~T(); return result;
      }
      result.push_back(left[i]); result.push_back(right[i]);
    }
  }
  engine->T::~T(); return result;
}
template<class T> void Test(const char* id, int bank, bool extended) {
  T capability;
  tzfm_count += capability.linear_tzfm_capable();
  fast_count += capability.fast_fm_capable();
  if (extended) Check(capability.linear_tzfm_capable() == bool(PLAITS_BUILD_EXTENDED_TZFM), id, "target qualification");
  uint64_t hash = 1469598103934665603ull;
  double difference = 0, direction_difference = 0;
  for (int s = 0; s < 2; ++s) for (int t = 0; t < 2; ++t)
  for (int k = 0; k < 3; ++k) {
    const float control = .15f + .35f * k;
    const std::vector<float> baseline = Render<T>(id, bank, control, s, t, 0);
    for (size_t n = 0; n < baseline.size(); ++n) {
      uint32_t bits; memcpy(&bits, &baseline[n], 4);
      hash = (hash ^ bits) * 1099511628211ull;
    }
#if PLAITS_BUILD_EXTENDED_TZFM
    if (extended) {
      const std::vector<float> stopped = Render<T>(id, bank, control, s, t, 1);
      const std::vector<float> negative = Render<T>(id, bank, control, s, t, 2);
      for (size_t n = 0; n < stopped.size() && n < negative.size(); ++n)
        difference += fabsf(stopped[n] - negative[n]);
      const std::vector<float> crossing = Render<T>(id, bank, control, s, t, 3);
      const std::vector<float> rectified = Render<T>(id, bank, control, s, t, 6);
      double phase_difference = 0;
      for (size_t n = 0; n < crossing.size() && n < rectified.size(); ++n)
        phase_difference += fabsf(crossing[n] - rectified[n]);
      // Aggregate below: some individual presets contain only unpitched noise.
      direction_difference += phase_difference;
      Render<T>(id, bank, control, s, t, 4, 96.f);
      Render<T>(id, bank, control, s, t, 5);
      Render<T>(id, bank, control, s, t, 7, 136.f);
    }
#endif
  }
#if PLAITS_BUILD_EXTENDED_TZFM
  if (extended) {
    Check(difference > .01, id, "negative frequency was clamped/stopped or ignored");
    Check(direction_difference > .01, id, "FM was rectified instead of reversing phase");
  }
#endif
  printf("%s %016llx signed_difference=%.9g\n", id, (unsigned long long)hash, difference);
  fflush(stdout);
}
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
class OffsetProbe : public Engine {
 public:
  void Init(stmlib::BufferAllocator*) { }
  void Reset() { }
  void LoadUserData(const uint8_t*) { }
  void Render(const EngineParameters& p, float* out, float* aux, size_t n, bool*) {
    for (size_t i = 0; i < n; ++i) out[i] = aux[i] = p.frequency_offset[i];
  }
};
#endif
// These analytic checks distinguish a reversing phase accumulator from an
// absolute-frequency oscillator (both can produce plausible-looking audio).
static void TestSignedPrimitives() {
#if PLAITS_BUILD_EXTENDED_TZFM
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  OffsetProbe probe; EngineParameters params = {};
  float offsets[12], main[12], aux[12];
  for (int i = 0; i < 12; ++i) offsets[i] = i;
  params.frequency_offset = offsets; params.hard_sync = 1u << 5;
  bool enveloped = false;
  RenderEngineWithHardSync(&probe, params, main, aux, 12, &enveloped);
  bool aligned = true;
  for (int i = 0; i < 12; ++i) aligned &= main[i] == offsets[i];
  Check(aligned, "sync", "fallback segments preserve FM sample alignment");
#endif
  // Exact sequence reversibility, not merely audible response to modulation.
  for (unsigned x = 1; x < 32768; ++x)
    Check(TzfmLfsrReverse(TzfmLfsrForward(x)) == x, "ZX noise", "inverse sequence");
  for (int n = 1; n <= 257; ++n) {
    uint32_t seed = 0xACE12345u + n;
    Check(TzfmLcgAdvance(TzfmLcgAdvance(seed, n), -n) == seed,
        "cymbal", "noise jump inverse");
  }
  float fraction = .375f;
  uint32_t counter = 3;
  counter += static_cast<uint32_t>(TzfmClock(-17.25f, &fraction));
  counter += static_cast<uint32_t>(TzfmClock(17.25f, &fraction));
  Check(counter == 3 && fraction == .375f, "bytebeat", "signed time including unsigned wrap");
  Check(TzfmIncrement(.125f) + TzfmIncrement(-.125f) == 0u,
      "integer phase", "negative conversion wraps exactly");
  pulse::PinChannel pin; pin.Init(); float naive = 0;
  pin.NextSigned(.125f, .2f, &naive);
  pin.NextSigned(-.125f, .2f, &naive);
  Check(pin.phase() == 0 && std::isfinite(pin.NextSigned(0, 0, &naive)),
      "ZX pulse", "phase reverses and zero-width pulse stays finite");
  LPCSpeechSynth speech; speech.Init();
  LPCSpeechSynth::Frame frame = {}; frame.energy = 128; frame.period = 80;
  speech.PlayFrame(&frame, 0.0f, false);
  float cycle[32], filtered;
  for (int i = 0; i < 32; ++i)
    speech.Render(0.0f, 2.5f, &cycle[i], &filtered, 1, 0.0f, true);
  for (int i = 0; i < 32; ++i) {
    float sample;
    speech.Render(0.0f, 2.5f, &sample, &filtered, 1, -0.0625f, true);
    Check(sample == cycle[(30 - i + 32) % 32], "LPC excitation", "reverse lookup retraces chirp cycle");
  }
  float held[2];
  speech.Render(0.0f, 2.5f, &held[0], &filtered, 1, -0.03125f, true);
  speech.Render(0.0f, 2.5f, &held[1], &filtered, 1, -0.03125f, true);
  Check(held[0] == held[1], "LPC excitation", "zero frequency holds cycle position");
  float phase = 0.125f;
  for (int i = 0; i < 100; ++i) phase = TzfmWrap(phase - 0.0625f);
  for (int i = 0; i < 100; ++i) phase = TzfmWrap(phase + 0.0625f);
  Check(phase == 0.125f, "phase", "forward/reverse round trip");
  float a = 0, b = 0, c = 0, d = 0;
  TzfmEdge(.9f, 1.1f, 0, 0, -1, 0, &a, &b);
  TzfmEdge(.1f, -.1f, 0, 0, -1, 0, &c, &d);
  Check(fabsf(a + c) < 1e-6f && fabsf(b + d) < 1e-6f && a != 0,
      "BLEP", "reverse crossing must invert the step correction");
  float master = .05f, slave = .8f;
  a = b = c = d = 0;
  TzfmSyncStep(-.2f, -.3f, 1, 0, 0, &master, &slave, &a, &b, &c, &d);
  Check(fabsf(master - .85f) < 1e-6f && fabsf(slave - .775f) < 1e-6f,
      "sync", "reverse reset must preserve the fractional sample time");
  fm::Operator op; op.Reset();
  float f = .0625f, gain = 1, feedback[2] = {}, output = 0;
  fm::RenderOperators<1, fm::Operator::MODULATION_SOURCE_NONE, false>(
      &op, &f, &gain, feedback, 0, NULL, &output, 1);
  Check(op.phase == 0x10000000u, "DX", "positive operator increment");
  f = -.0625f;
  fm::RenderOperators<1, fm::Operator::MODULATION_SOURCE_NONE, false>(
      &op, &f, &gain, feedback, 0, NULL, &output, 1);
  Check(op.phase == 0, "DX", "signed operator phase must return to start");
  f = 0;
  fm::RenderOperators<1, fm::Operator::MODULATION_SOURCE_NONE, false>(
      &op, &f, &gain, feedback, 0, NULL, &output, 1);
  Check(op.phase == 0, "DX", "zero operator increment must hold phase");
#endif
}

int main() {
  Test<VirtualAnalogEngine>("virtual-analog", -1, false);
  Test<VirtualAnalogDualEngine>("virtual-analog-dual", -1, false);
  Test<VirtualAnalogCrossfadeEngine>("virtual-analog-crossfade", -1, false);
  Test<WaveshapingEngine>("waveshaping", -1, false);
  Test<FMEngine>("two-op-fm", -1, false);
  Test<GrainEngine>("granular-formant", -1, true);
  Test<AdditiveEngine>("harmonic", -1, false);
  Test<WavetableEngine>("wavetable", -1, false);
  Test<ChordEngine>("chords", -1, true);
  Test<SpeechEngine>("speech", -1, true);
  Test<FormantSpeechEngine>("formant-speech", -1, true);
  Test<LPCSpeechEngine>("lpc-speech", -1, true);
  Test<SwarmEngine>("swarm", -1, false);
  Test<NoiseEngine>("filtered-noise", -1, false);
  Test<ParticleEngine>("particle-noise", -1, false);
  Test<StringEngine>("inharmonic-string", -1, false);
  Test<ModalEngine>("modal-resonator", -1, false);
  Test<BassDrumEngine>("analog-bass-drum", -1, false);
  Test<SnareDrumEngine>("analog-snare", -1, false);
  Test<HiHatEngine>("analog-hi-hat", -1, true);
  Test<VirtualAnalogVCFEngine>("virtual-analog-vcf", -1, false);
  Test<PhaseDistortionEngine>("phase-distortion", -1, false);
  Test<SixOpEngine>("dx7-bank-a", 0, true);
  Test<SixOpEngine>("dx7-bank-b", 1, true);
  Test<SixOpEngine>("dx7-bank-c", 2, true);
  Test<StringMachineEngine>("string-machine", -1, true);
  Test<ChiptuneEngine>("chiptune", -1, true);
  Test<NaturalSpeechEngine>("natural-speech", -1, true);
  Test<GlissonEngine>("glisson", -1, true);
  Test<GendyEngine>("gendy", -1, true);
  Test<ScannedEngine>("scanned", -1, true);
  Test<PulsarEngine>("pulsar", -1, false);
  Test<LoopbackEngine>("loopback", -1, false);
  Test<LockstepEngine>("lockstep", -1, true);
  Test<TapfieldEngine>("tapfield", -1, false);
  Test<PhaseWeaveEngine>("phase-weave", -1, false);
  Test<SidebandEngine>("sideband-bank", -1, false);
  Test<AttractorEngine>("attractor", -1, false);
  Test<UndertowEngine>("undertow", -1, true);
  Test<ReedPipeEngine>("reed-pipe", -1, false);
  Test<PhaseFlockEngine>("phase-flock", -1, false);
  Test<RulefieldEngine>("rulefield", -1, true);
  Test<SpectralSpiralEngine>("spectral-spiral", -1, false);
  Test<ZFilterEngine>("z-filter", -1, true);
  Test<ToyEngine>("toy", -1, false);
  Test<CSawEngine>("csaw", -1, true);
  Test<RingModEngine>("ring-mod", -1, false);
  Test<FoldEngine>("fold", -1, false);
  Test<BuzzEngine>("buzz", -1, false);
  Test<DualSyncEngine>("dual-sync", -1, true);
  Test<GranularCloudEngine>("granular-cloud", -1, true);
  Test<MorphEngine>("morph", -1, true);
  Test<NoiseBankEngine>("noise-bank", -1, false);
  Test<ParticleBurstEngine>("particle-burst", -1, false);
  Test<SawSquareEngine>("saw-square", -1, true);
  Test<SawSwarmEngine>("saw-swarm", -1, false);
  Test<VowelEngine>("vowel", -1, true);
  Test<HarmonicsEngine>("harmonics", -1, false);
  Test<VosimEngine>("vosim", -1, false);
  Test<PluckedEngine>("plucked", -1, false);
  Test<BlownEngine>("blown", -1, false);
  Test<StruckBellEngine>("struck-bell", -1, true);
  Test<StruckDrumEngine>("struck-drum", -1, true);
  Test<KickEngine>("kick", -1, false);
  Test<SnareEngine>("snare", -1, false);
  Test<CymbalEngine>("cymbal", -1, true);
  Test<WaveScanEngine>("wave-scan", -1, false);
  Test<WaveParaphonicEngine>("wave-paraphonic", -1, true);
  Test<FlutedEngine>("fluted", -1, false);
  Test<QuestionMarkEngine>("question-mark", -1, true);
  Test<BowedEngine>("bowed", -1, false);
  Test<SubOscillatorEngine>("sub-oscillator", -1, true);
  Test<DigitalModulationEngine>("digital-modulation", -1, false);
  Test<SawCombEngine>("saw-comb", -1, true);
  Test<VowelFofEngine>("vowel-fof", -1, false);
  Test<RawFmEngine>("raw-fm", -1, false);
  Test<TripleEngine>("triple", -1, false);
  Test<BytebeatEngine>("bytebeat", -1, true);
  Test<DiatonicChordEngine>("diatonic-chord", -1, true);
  Test<ScaleStackEngine>("scale-stack", -1, true);
  Test<WavetableChordEngine>("wavetable-chord", -1, true);
  Test<WavetableScaleStackEngine>("wavetable-scale-stack", -1, true);
  Test<ShakersEngine>("shakers", -1, false);
  Test<BrassEngine>("brass", -1, false);
  Test<HelixEngine>("helix", -1, true);
  Test<SkinsEngine>("skins", -1, true);
  Test<CircuitZapsEngine>("circuit-zaps", -1, true);
  Test<MetalworkEngine>("metalwork", -1, true);
  Test<ClapEngine>("clap", -1, false);
  Test<AnalogPercussionEngine>("analog-percussion", -1, true);
  Test<FreshetsFormantEngine>("freshets-formant", -1, true);
  Test<BubbleTimeEngine>("bubbletime", -1, true);
  Test<ZxPhase48kEngine>("zxphase48k", -1, true);
  Test<ZxPulse48kEngine>("zxpulse48k", -1, true);
  Test<AcidEngine>("acid", -1, true);
  // Wave Terrain needs a bank, so its baseline audio is covered by the host
  // parity suite; include its unchanged eligibility in the whole-catalog count.
  WaveTerrainEngine terrain;
  tzfm_count += terrain.linear_tzfm_capable();
  fast_count += terrain.fast_fm_capable();
  Check(tzfm_count == (PLAITS_BUILD_EXTENDED_TZFM ? 76 : 29), "catalog", "TZFM target count");
  Check(fast_count == 34, "catalog", "Plaits Fast FM qualification must stay unchanged");
  TestSignedPrimitives();
  Check(!TapfieldEngine().linear_tzfm_capable(), "tapfield", "corruption is not reversible");
  Check(!AttractorEngine().linear_tzfm_capable(), "attractor", "negative time reverses damping");
#if PLAITS_BUILD_EXTENDED_TZFM
  for (int k = 0; k <= 6; ++k) {
    const float knob = k / 6.0f;
    const std::vector<float> dry = Render<BubbleTimeEngine>("bubble-gates", -1, knob, false, true, 0);
    const std::vector<float> fm = Render<BubbleTimeEngine>("bubble-gates", -1, knob, false, true, 3);
    bool equal = dry.size() == fm.size();
    for (size_t n = 1; equal && n < dry.size(); n += 2) equal = dry[n] == fm[n];
    Check(equal, "bubbletime", "FM must not alter gate/rhythm timing");
  }
  // Every speech blend region, word-bank region, and extreme formant shift.
  for (int k = 0; k <= 12; ++k) {
    const float control = k / 12.0f;
    for (int mode = 1; mode <= 5; ++mode) {
      Render<SpeechEngine>("speech-all-branches", -1, control, k & 1, true, mode);
      Render<FormantSpeechEngine>("speech-sounds-all-branches", -1, control, k & 1, false, mode);
      Render<LPCSpeechEngine>("lpc-all-banks", -1, control, k & 1, true, mode);
      Render<NaturalSpeechEngine>("natural-all-controls", -1, control, k & 1, true, mode);
    }
  }
  // Exercise every factory DX patch; algorithms and fixed-Hz operators vary
  // across banks, so three generic knob positions are insufficient here.
  for (int bank = 0; bank < 3; ++bank) for (int patch = 0; patch < 32; ++patch) {
    const float knob = float(patch) / (31.0f * 1.02f);
    Render<SixOpEngine>("dx-all-patches", bank, knob, false, true, 3);
  }
#endif
  printf("TZFM=%d FastFM=%d failures=%d\n", tzfm_count, fast_count, failures);
  return failures ? 1 : 0;
}
