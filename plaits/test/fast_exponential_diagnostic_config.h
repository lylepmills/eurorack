// Private qualification registry for non-TZFM engines with newly implemented
// per-sample exponential pitch paths. The first stage applies the ordinary
// block-rate exponential FM law; the second applies the same law through the
// fast converter. No engine in this file is product-qualified by this build.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 0
#define PLAITS_BUILD_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_FORCE_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_EXPONENTIAL 1
#define PLAITS_TZFM_DIAGNOSTIC 1
#define PLAITS_TZFM_AUDITION_GROUP 8
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 1
#define PLAITS_CPU_PROBE_AUX 0

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine2/csaw_engine.h"
#include "plaits/dsp/engine2/dual_sync_engine.h"
#include "plaits/dsp/engine2/morph_engine.h"
#include "plaits/dsp/engine2/saw_square_engine.h"
#include "plaits/dsp/engine2/vowel_engine.h"
#include "plaits/dsp/engine2/sub_oscillator_engine.h"
#include "plaits/dsp/engine2/gendy_engine.h"
#include "plaits/dsp/engine2/bytebeat_engine.h"

#define PLAITS_ENGINE_COUNT 8
#define PLAITS_BANK_SIZES { 8 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7 }

#define PLAITS_ENGINE_MEMBERS \
  CSawEngine csaw_engine_; \
  DualSyncEngine dual_sync_engine_; \
  MorphEngine morph_engine_; \
  SawSquareEngine saw_square_engine_; \
  VowelEngine vowel_engine_; \
  SubOscillatorEngine sub_oscillator_engine_; \
  GendyEngine gendy_engine_; \
  BytebeatEngine bytebeat_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&csaw_engine_, false, 0.73f, 0.73f); \
  (registry).RegisterInstance(&dual_sync_engine_, false, -0.9f, -0.9f); \
  (registry).RegisterInstance(&morph_engine_, false, -0.45f, -0.45f); \
  (registry).RegisterInstance(&saw_square_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&vowel_engine_, false, -0.7f, -1.25f); \
  (registry).RegisterInstance(&sub_oscillator_engine_, false, -0.45f, -0.45f); \
  (registry).RegisterInstance(&gendy_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&bytebeat_engine_, false, -0.9f, -0.9f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
