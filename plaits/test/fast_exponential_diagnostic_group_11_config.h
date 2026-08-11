// Private qualification registry for the fourth batch of non-TZFM engines
// with positive-frequency per-sample pitch paths. No engine in this file is
// product-qualified by this build.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 0
#define PLAITS_BUILD_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_FORCE_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_EXPONENTIAL 1
#define PLAITS_TZFM_DIAGNOSTIC 1
#define PLAITS_TZFM_AUDITION_GROUP 11
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 1
#define PLAITS_CPU_PROBE_AUX 0

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"

#define PLAITS_ENGINE_COUNT 8
#define PLAITS_BANK_SIZES { 8 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7 }

#define PLAITS_ENGINE_MEMBERS \
  ModalEngine modal_engine_; \
  StringEngine string_engine_; \
  NoiseEngine noise_engine_; \
  ParticleEngine particle_engine_; \
  BassDrumEngine bass_drum_engine_; \
  SnareDrumEngine snare_drum_engine_; \
  HiHatEngine hi_hat_engine_; \
  StringMachineEngine string_machine_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&modal_engine_, true, -1.0f, 0.8f); \
  (registry).RegisterInstance(&string_engine_, true, -1.0f, 0.8f); \
  (registry).RegisterInstance(&noise_engine_, false, -1.0f, -1.0f); \
  (registry).RegisterInstance(&particle_engine_, false, -2.0f, 1.0f); \
  (registry).RegisterInstance(&bass_drum_engine_, true, 0.8f, 0.8f); \
  (registry).RegisterInstance(&snare_drum_engine_, true, 0.8f, 0.8f); \
  (registry).RegisterInstance(&hi_hat_engine_, true, 0.8f, 0.8f); \
  (registry).RegisterInstance(&string_machine_engine_, false, 0.8f, 0.8f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
