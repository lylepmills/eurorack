// Private hardware qualification for Acid's signed-frequency oscillator path.
// The force flag reaches the new path without granting product capability.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 1
#define PLAITS_BUILD_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_FORCE_LINEAR_TZFM 1
#define PLAITS_TZFM_DIAGNOSTIC 1
#define PLAITS_TZFM_DIAGNOSTIC_STEREO 1
#define PLAITS_TZFM_AUDITION_GROUP 7
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 1
#define PLAITS_CPU_PROBE_AUX 0

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine2/acid_engine.h"

#define PLAITS_ENGINE_COUNT 1
#define PLAITS_BANK_SIZES { 1 }
#define PLAITS_ENGINE_ROWS { 0 }

#define PLAITS_ENGINE_MEMBERS AcidEngine acid_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&acid_engine_, false, 0.9f, 0.9f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
