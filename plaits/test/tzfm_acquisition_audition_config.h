// Compact engine registry for the linear-TZFM acquisition hardware audition.
// Every bank repeats the same three rows, so banked navigation changes only
// the acquisition strategy and preserves the selected synthesis model.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_ENGINE_COUNT 9
#define PLAITS_BANK_SIZES { 3, 3, 3 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 0, 1, 2, 0, 1, 2 }
#define PLAITS_BUILD_LINEAR_TZFM 1
#define PLAITS_INPUT_FAULT_DIAGNOSTICS 1
#define PLAITS_BUILD_NAVIGATION_MODE 1

// Preserve the regular bank/model and ADC-fault LEDs. AUX becomes a precise
// whole-callback CPU meter: measured usage u is reported as u * 1000 Hz.
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 0
#define PLAITS_CPU_PROBE_AUX 1
#define PLAITS_CPU_PROBE_WHOLE_CALLBACK 1

// Internal three-bank registry order is amber, green, red. Public audition:
//   green (indices 3..5): uninterrupted fast FM, no LEVEL
//   red   (indices 6..8): stock control-rate FM + LEVEL scan
//   amber (indices 0..2): fast FM periodically interrupted for LEVEL
#define PLAITS_TZFM_CONTROL_RATE_ENGINE_MASK 0x1c0u
#define PLAITS_TZFM_INTERRUPTED_LEVEL_ENGINE_MASK 0x007u

#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine2/vowel_fof_engine.h"

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#define PLAITS_ENGINE_MEMBERS \
  WaveshapingEngine waveshaping_engine_; \
  FMEngine fm_engine_; \
  VowelFofEngine vowel_fof_engine_;

#define PLAITS_TZFM_AUDITION_BANK(registry) do { \
  (registry).RegisterInstance(&waveshaping_engine_, false, 0.7f, 0.6f); \
  (registry).RegisterInstance(&fm_engine_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&vowel_fof_engine_, false, 0.8f, 0.8f); \
} while (0)

#define PLAITS_REGISTER_ENGINES(registry) do { \
  PLAITS_TZFM_AUDITION_BANK(registry); \
  PLAITS_TZFM_AUDITION_BANK(registry); \
  PLAITS_TZFM_AUDITION_BANK(registry); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
