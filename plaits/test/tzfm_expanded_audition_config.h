// Compact hardware-audition registries for oscillator engines with native
// signed-frequency paths. Select group 1 through 4 with
// -DPLAITS_TZFM_AUDITION_GROUP=<n>. Each build contains a single bank so the
// models advance with one button press and the audio updater stays short.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#ifndef PLAITS_TZFM_AUDITION_GROUP
#define PLAITS_TZFM_AUDITION_GROUP 1
#endif

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0
#define PLAITS_BUILD_LINEAR_TZFM 1
#define PLAITS_BUILD_FAST_FM 0

#if PLAITS_TZFM_AUDITION_GROUP == 1

#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/virtual_analog_dual_engine.h"
#include "plaits/dsp/engine/virtual_analog_crossfade_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine/additive_engine.h"

#define PLAITS_ENGINE_COUNT 8
#define PLAITS_BANK_SIZES { 8 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7 }
#define PLAITS_ENGINE_MEMBERS \
  VirtualAnalogEngine virtual_analog_engine_; \
  VirtualAnalogDualEngine virtual_analog_dual_engine_; \
  VirtualAnalogCrossfadeEngine virtual_analog_crossfade_engine_; \
  VirtualAnalogVCFEngine virtual_analog_vcf_engine_; \
  WaveshapingEngine waveshaping_engine_; \
  PhaseDistortionEngine phase_distortion_engine_; \
  WavetableEngine wavetable_engine_; \
  AdditiveEngine additive_engine_;
#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&virtual_analog_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_dual_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_crossfade_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_vcf_engine_, false, 1.0f, 1.0f); \
  (registry).RegisterInstance(&waveshaping_engine_, false, 0.7f, 0.6f); \
  (registry).RegisterInstance(&phase_distortion_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&wavetable_engine_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&additive_engine_, false, 0.8f, 0.8f); \
} while (0)

#elif PLAITS_TZFM_AUDITION_GROUP == 2

#include "plaits/dsp/engine2/harmonics_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine2/raw_fm_engine.h"
#include "plaits/dsp/engine2/ring_mod_engine.h"
#include "plaits/dsp/engine2/sideband_engine.h"
#include "plaits/dsp/engine2/vowel_fof_engine.h"
#include "plaits/dsp/engine2/digital_modulation_engine.h"
#include "plaits/dsp/engine2/loopback_engine.h"

#define PLAITS_ENGINE_COUNT 8
#define PLAITS_BANK_SIZES { 8 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7 }
#define PLAITS_ENGINE_MEMBERS \
  HarmonicsEngine harmonics_engine_; \
  FMEngine fm_engine_; \
  RawFmEngine raw_fm_engine_; \
  RingModEngine ring_mod_engine_; \
  SidebandEngine sideband_engine_; \
  VowelFofEngine vowel_fof_engine_; \
  DigitalModulationEngine digital_modulation_engine_; \
  LoopbackEngine loopback_engine_;
#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&harmonics_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&fm_engine_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&raw_fm_engine_, false, -0.45f, -0.45f); \
  (registry).RegisterInstance(&ring_mod_engine_, false, -1.5f, -1.5f); \
  (registry).RegisterInstance(&sideband_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&vowel_fof_engine_, false, -3.8f, -3.8f); \
  (registry).RegisterInstance(&digital_modulation_engine_, false, -0.55f, -0.5f); \
  (registry).RegisterInstance(&loopback_engine_, false, 0.8f, 0.8f); \
} while (0)

#elif PLAITS_TZFM_AUDITION_GROUP == 3

#include "plaits/dsp/engine2/phase_weave_engine.h"
#include "plaits/dsp/engine2/phase_flock_engine.h"
#include "plaits/dsp/engine2/spectral_spiral_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"
#include "plaits/dsp/engine2/fold_engine.h"
#include "plaits/dsp/engine2/buzz_engine.h"
#include "plaits/dsp/engine2/pulsar_engine.h"
#include "plaits/dsp/engine2/toy_engine.h"

#define PLAITS_ENGINE_COUNT 8
#define PLAITS_BANK_SIZES { 8 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7 }
#define PLAITS_ENGINE_MEMBERS \
  PhaseWeaveEngine phase_weave_engine_; \
  PhaseFlockEngine phase_flock_engine_; \
  SpectralSpiralEngine spectral_spiral_engine_; \
  WaveTerrainEngine wave_terrain_engine_; \
  FoldEngine fold_engine_; \
  BuzzEngine buzz_engine_; \
  PulsarEngine pulsar_engine_; \
  ToyEngine toy_engine_;
#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&phase_weave_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&phase_flock_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&spectral_spiral_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&wave_terrain_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&fold_engine_, false, -0.7f, -0.7f); \
  (registry).RegisterInstance(&buzz_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&pulsar_engine_, false, 0.9f, 0.9f); \
  (registry).RegisterInstance(&toy_engine_, false, -0.85f, -0.85f); \
} while (0)

#elif PLAITS_TZFM_AUDITION_GROUP == 4

#include "plaits/dsp/engine2/saw_swarm_engine.h"
#include "plaits/dsp/engine2/triple_engine.h"
#include "plaits/dsp/engine2/vosim_engine.h"
#include "plaits/dsp/engine2/wave_scan_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"

#define PLAITS_ENGINE_COUNT 5
#define PLAITS_BANK_SIZES { 5 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4 }
#define PLAITS_ENGINE_MEMBERS \
  SawSwarmEngine saw_swarm_engine_; \
  TripleEngine triple_engine_; \
  VosimEngine vosim_engine_; \
  WaveScanEngine wave_scan_engine_; \
  SwarmEngine swarm_engine_;
#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&saw_swarm_engine_, false, -0.7f, -0.7f); \
  (registry).RegisterInstance(&triple_engine_, false, -0.6f, -0.6f); \
  (registry).RegisterInstance(&vosim_engine_, false, -0.8f, -0.8f); \
  (registry).RegisterInstance(&wave_scan_engine_, false, -1.0f, -1.0f); \
  (registry).RegisterInstance(&swarm_engine_, false, -3.0f, 1.0f); \
} while (0)

#else
#error "PLAITS_TZFM_AUDITION_GROUP must be 1, 2, 3, or 4"
#endif

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
