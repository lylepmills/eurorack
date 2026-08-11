// Private qualification registry for the third batch of non-TZFM engines
// with per-sample exponential pitch paths. No engine in this file is product-
// qualified by this build.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 0
#define PLAITS_BUILD_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_FORCE_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_EXPONENTIAL 1
#define PLAITS_TZFM_DIAGNOSTIC 1
#define PLAITS_TZFM_AUDITION_GROUP 10
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 1
#define PLAITS_CPU_PROBE_AUX 0

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine2/undertow_engine.h"
#include "plaits/dsp/engine2/reed_pipe_engine.h"
#include "plaits/dsp/engine2/z_filter_engine.h"
#include "plaits/dsp/engine2/granular_cloud_engine.h"
#include "plaits/dsp/engine2/noise_bank_engine.h"
#include "plaits/dsp/engine2/particle_burst_engine.h"
#include "plaits/dsp/engine2/plucked_engine.h"
#include "plaits/dsp/engine2/blown_engine.h"

#define PLAITS_ENGINE_COUNT 8
#define PLAITS_BANK_SIZES { 8 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7 }

#define PLAITS_ENGINE_MEMBERS \
  UndertowEngine undertow_engine_; \
  ReedPipeEngine reed_pipe_engine_; \
  ZFilterEngine z_filter_engine_; \
  GranularCloudEngine granular_cloud_engine_; \
  NoiseBankEngine noise_bank_engine_; \
  ParticleBurstEngine particle_burst_engine_; \
  PluckedEngine plucked_engine_; \
  BlownEngine blown_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&undertow_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&reed_pipe_engine_, false, -1.0f, -1.0f); \
  (registry).RegisterInstance(&z_filter_engine_, false, -0.6f, -0.6f); \
  (registry).RegisterInstance(&granular_cloud_engine_, false, -0.8f, -0.8f); \
  (registry).RegisterInstance(&noise_bank_engine_, false, -0.8f, -0.8f); \
  (registry).RegisterInstance(&particle_burst_engine_, false, -1.0f, -1.0f); \
  (registry).RegisterInstance(&plucked_engine_, false, -1.0f, -1.0f); \
  (registry).RegisterInstance(&blown_engine_, false, -1.0f, -1.0f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
