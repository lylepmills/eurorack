// Private qualification registry for the second batch of non-TZFM engines
// with per-sample exponential pitch paths. No engine in this file is product-
// qualified by this build.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 0
#define PLAITS_BUILD_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_FORCE_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_EXPONENTIAL 1
#define PLAITS_TZFM_DIAGNOSTIC 1
#define PLAITS_TZFM_AUDITION_GROUP 9
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 1
#define PLAITS_CPU_PROBE_AUX 0

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine2/glisson_engine.h"
#include "plaits/dsp/engine2/scanned_engine.h"
#include "plaits/dsp/engine2/lockstep_engine.h"
#include "plaits/dsp/engine2/tapfield_engine.h"
#include "plaits/dsp/engine2/attractor_engine.h"
#include "plaits/dsp/engine2/rulefield_engine.h"
#include "plaits/dsp/engine2/question_mark_engine.h"
#include "plaits/dsp/engine2/freshets_formant_engine.h"

#define PLAITS_ENGINE_COUNT 8
#define PLAITS_BANK_SIZES { 8 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7 }

#define PLAITS_ENGINE_MEMBERS \
  GlissonEngine glisson_engine_; \
  ScannedEngine scanned_engine_; \
  LockstepEngine lockstep_engine_; \
  TapfieldEngine tapfield_engine_; \
  AttractorEngine attractor_engine_; \
  RulefieldEngine rulefield_engine_; \
  QuestionMarkEngine question_mark_engine_; \
  FreshetsFormantEngine freshets_formant_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&glisson_engine_, false, 0.9f, 0.9f); \
  (registry).RegisterInstance(&scanned_engine_, false, -1.0f, -1.0f); \
  (registry).RegisterInstance(&lockstep_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&tapfield_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&attractor_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&rulefield_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&question_mark_engine_, false, -0.7f, -0.7f); \
  (registry).RegisterInstance( \
      &freshets_formant_engine_, false, 0.9f, 0.55f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
