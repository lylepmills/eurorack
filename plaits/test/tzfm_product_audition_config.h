// Compact hardware-audition registry for the independent experimental FM
// preferences. Build it once with PLAITS_BUILD_LINEAR_TZFM only and once with
// both PLAITS_BUILD_LINEAR_TZFM and PLAITS_BUILD_FAST_FM. The three models then
// advance with one button press: Waveshaping, Two-op FM, Vowel FOF.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine2/vowel_fof_engine.h"

#define PLAITS_ENGINE_COUNT 3
#define PLAITS_BANK_SIZES { 3 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2 }
#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#define PLAITS_ENGINE_MEMBERS \
  WaveshapingEngine waveshaping_engine_; \
  FMEngine fm_engine_; \
  VowelFofEngine vowel_fof_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&waveshaping_engine_, false, 0.7f, 0.6f); \
  (registry).RegisterInstance(&fm_engine_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&vowel_fof_engine_, false, 0.8f, 0.8f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
