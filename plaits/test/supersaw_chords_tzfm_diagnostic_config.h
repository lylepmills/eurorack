// Private hardware qualification for Supersaw Chords' signed-frequency path
// (linear TZFM: the saws run backwards through zero). The force flag reaches
// the new path without granting product capability. The same engine fills
// three slots with MACRO pinned to one, two and three saws per chord tone,
// because the stack size is what sets this engine's cost.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 1
#define PLAITS_BUILD_FAST_FM 1
#define PLAITS_FM_DIAGNOSTIC_FORCE_LINEAR_TZFM 1
#define PLAITS_TZFM_DIAGNOSTIC 1
#define PLAITS_TZFM_DIAGNOSTIC_STEREO 1
#define PLAITS_TZFM_AUDITION_GROUP 16
#define PLAITS_TZFM_DIAGNOSTIC_MACROS { 0.02f, 0.5f, 0.98f }
#define PLAITS_CPU_PROBE 1
#define PLAITS_CPU_PROBE_LEDS 1
#define PLAITS_CPU_PROBE_AUX 0

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine2/chords_supersaw_engine.h"

#define PLAITS_ENGINE_COUNT 3
#define PLAITS_BANK_SIZES { 3 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2 }

#define PLAITS_ENGINE_MEMBERS \
  ChordsSupersawEngine chords_supersaw_stack_1_; \
  ChordsSupersawEngine chords_supersaw_stack_2_; \
  ChordsSupersawEngine chords_supersaw_stack_3_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&chords_supersaw_stack_1_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&chords_supersaw_stack_2_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&chords_supersaw_stack_3_, false, 0.8f, 0.8f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
