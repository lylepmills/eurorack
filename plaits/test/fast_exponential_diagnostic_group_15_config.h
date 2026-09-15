// Qualification registry for post-August-11 engines with implemented
// positive-frequency per-sample pitch paths. It stays intact as the
// reproducible benchmark even after an individual engine is product-qualified.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 0
#define PLAITS_BUILD_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_FORCE_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_EXPONENTIAL 1
#define PLAITS_TZFM_DIAGNOSTIC 1
#define PLAITS_TZFM_DIAGNOSTIC_STEREO 1
#define PLAITS_TZFM_AUDITION_GROUP 15
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 1
#define PLAITS_CPU_PROBE_AUX 0

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine2/analog_percussion_engine.h"
#include "plaits/dsp/engine2/skins_engine.h"
#include "plaits/dsp/engine2/circuit_zaps_engine.h"
#include "plaits/dsp/engine2/metalwork_engine.h"
#include "plaits/dsp/engine2/zxpulse48k_engine.h"
#include "plaits/dsp/engine2/acid_engine.h"

#define PLAITS_ENGINE_COUNT 6
#define PLAITS_BANK_SIZES { 6 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5 }

#define PLAITS_ENGINE_MEMBERS \
  AnalogPercussionEngine analog_percussion_engine_; \
  SkinsEngine skins_engine_; \
  CircuitZapsEngine circuit_zaps_engine_; \
  MetalworkEngine metalwork_engine_; \
  ZxPulse48kEngine zxpulse48k_engine_; \
  AcidEngine acid_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&analog_percussion_engine_, true, 0.8f, 0.8f); \
  (registry).RegisterInstance(&skins_engine_, true, 0.9f, 0.9f); \
  (registry).RegisterInstance(&circuit_zaps_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&metalwork_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&zxpulse48k_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&acid_engine_, false, 0.9f, 0.9f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
