// Private qualification registry for the sixth batch of non-TZFM engines
// with positive-frequency per-sample pitch paths. No engine in this file is
// product-qualified by this build.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 0
#define PLAITS_BUILD_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_FORCE_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_EXPONENTIAL 1
#define PLAITS_TZFM_DIAGNOSTIC 1
#define PLAITS_TZFM_AUDITION_GROUP 13
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 1
#define PLAITS_CPU_PROBE_AUX 0

#define PLAITS_HAS_SPEECH_ENGINE 1
#define PLAITS_HAS_LPC_WORDS_ENGINE 1
#define PLAITS_HAS_CHIPTUNE_ENGINE 1
#define PLAITS_HAS_USER_DATA_BANK 1
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine2/formant_speech_engine.h"
#include "plaits/dsp/engine2/lpc_speech_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/chiptune_engine.h"

#define PLAITS_ENGINE_COUNT 9
#define PLAITS_BANK_SIZES { 8, 1 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7, 0 }

#define PLAITS_ENGINE_MEMBERS \
  GrainEngine grain_engine_; \
  ChordEngine chord_engine_; \
  SpeechEngine speech_engine_; \
  FormantSpeechEngine formant_speech_engine_; \
  LPCSpeechEngine lpc_speech_engine_; \
  SixOpEngine six_op_engine_; \
  ChiptuneEngine chiptune_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&grain_engine_, false, 0.7f, 0.6f); \
  (registry).RegisterInstance(&chord_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&speech_engine_, false, -0.7f, 0.8f); \
  (registry).RegisterInstance(&formant_speech_engine_, false, -0.7f, 0.8f); \
  (registry).RegisterInstance(&lpc_speech_engine_, false, -0.7f, 0.8f); \
  (registry).RegisterInstance(&six_op_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&six_op_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&six_op_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&chiptune_engine_, false, 0.5f, 0.5f); \
} while (0)

namespace plaits {

static const int8_t kEngineUserDataBank[9] = {
  -1, -1, -1, -1, -1, 0, 1, 2, -1,
};
static const uint32_t kSpeechEngineMask = 1u << 2;
static const uint32_t kLPCWordsEngineMask = 1u << 4;
static const uint32_t kChiptuneEngineMask = 1u << 8;

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
