// Private qualification registry for the fifth batch of non-TZFM engines
// with positive-frequency per-sample pitch paths. No engine in this file is
// product-qualified by this build.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 0
#define PLAITS_BUILD_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_FORCE_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_EXPONENTIAL 1
#define PLAITS_TZFM_DIAGNOSTIC 1
#define PLAITS_TZFM_AUDITION_GROUP 12
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 1
#define PLAITS_CPU_PROBE_AUX 0

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine2/struck_bell_engine.h"
#include "plaits/dsp/engine2/struck_drum_engine.h"
#include "plaits/dsp/engine2/kick_engine.h"
#include "plaits/dsp/engine2/snare_engine.h"
#include "plaits/dsp/engine2/cymbal_engine.h"
#include "plaits/dsp/engine2/wave_paraphonic_engine.h"
#include "plaits/dsp/engine2/fluted_engine.h"
#include "plaits/dsp/engine2/bowed_engine.h"

#define PLAITS_ENGINE_COUNT 8
#define PLAITS_BANK_SIZES { 8 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7 }

#define PLAITS_ENGINE_MEMBERS \
  StruckBellEngine struck_bell_engine_; \
  StruckDrumEngine struck_drum_engine_; \
  KickEngine kick_engine_; \
  SnareEngine snare_engine_; \
  CymbalEngine cymbal_engine_; \
  WaveParaphonicEngine wave_paraphonic_engine_; \
  FlutedEngine fluted_engine_; \
  BowedEngine bowed_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&struck_bell_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&struck_drum_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&kick_engine_, true, 1.0f, 0.8f); \
  (registry).RegisterInstance(&snare_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&cymbal_engine_, false, 1.0f, 1.0f); \
  (registry).RegisterInstance(&wave_paraphonic_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&fluted_engine_, false, -0.8f, -0.8f); \
  (registry).RegisterInstance(&bowed_engine_, false, -0.85f, -0.85f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
