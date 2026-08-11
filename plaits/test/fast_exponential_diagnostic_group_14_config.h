// Private qualification registry for the final batch of non-TZFM engines
// with positive-frequency per-sample pitch paths. No engine in this file is
// product-qualified by this build.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 0
#define PLAITS_BUILD_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_FORCE_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_EXPONENTIAL 1
#define PLAITS_TZFM_DIAGNOSTIC 1
#define PLAITS_TZFM_AUDITION_GROUP 14
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 1
#define PLAITS_CPU_PROBE_AUX 0

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine2/saw_comb_engine.h"
#include "plaits/dsp/engine2/diatonic_chord_engine.h"
#include "plaits/dsp/engine2/scale_stack_engine.h"
#include "plaits/dsp/engine2/wavetable_chord_engine.h"
#include "plaits/dsp/engine2/wavetable_scale_stack_engine.h"
#include "plaits/dsp/engine2/shakers_engine.h"
#include "plaits/dsp/engine2/brass_engine.h"
#include "plaits/dsp/engine2/helix_engine.h"
#include "plaits/dsp/engine2/clap_engine.h"

#define PLAITS_ENGINE_COUNT 9
#define PLAITS_BANK_SIZES { 8, 1 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7, 0 }

#define PLAITS_ENGINE_MEMBERS \
  SawCombEngine saw_comb_engine_; \
  DiatonicChordEngine diatonic_chord_engine_; \
  ScaleStackEngine scale_stack_engine_; \
  WavetableChordEngine wavetable_chord_engine_; \
  WavetableScaleStackEngine wavetable_scale_stack_engine_; \
  ShakersEngine shakers_engine_; \
  BrassEngine brass_engine_; \
  HelixEngine helix_engine_; \
  ClapEngine clap_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&saw_comb_engine_, false, -0.85f, -0.85f); \
  (registry).RegisterInstance(&diatonic_chord_engine_, false, -0.65f, -0.65f); \
  (registry).RegisterInstance(&scale_stack_engine_, false, -0.6f, -0.6f); \
  (registry).RegisterInstance(&wavetable_chord_engine_, false, -0.65f, -0.65f); \
  (registry).RegisterInstance(&wavetable_scale_stack_engine_, false, -0.6f, -0.6f); \
  (registry).RegisterInstance(&shakers_engine_, true, 0.8f, 0.8f); \
  (registry).RegisterInstance(&brass_engine_, false, -1.0f, 0.8f); \
  (registry).RegisterInstance(&helix_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&clap_engine_, true, 0.8f, 0.8f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
