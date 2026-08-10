// Private hardware-audition registry for the eight engines that completed the
// slow-converter TZFM stress matrix but exceeded a deadline or lost FM
// transport in fast mode. Hardware listening subsequently qualified all eight
// for the explicitly Experimental Fast FM option. This remains a compact
// audition registry rather than a hosted recipe configuration.
#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_BUILD_LINEAR_TZFM 1
#define PLAITS_BUILD_FAST_FM 1

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_RESOLVED_USER_DATA_BANK 0

#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine2/ring_mod_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine2/vowel_fof_engine.h"
#include "plaits/dsp/engine2/phase_weave_engine.h"
#include "plaits/dsp/engine2/buzz_engine.h"
#include "plaits/dsp/engine2/vosim_engine.h"

#define PLAITS_ENGINE_COUNT 8
#define PLAITS_BANK_SIZES { 8 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6, 7 }

#define PLAITS_ENGINE_MEMBERS \
  VirtualAnalogEngine virtual_analog_engine_; \
  FMEngine fm_engine_; \
  RingModEngine ring_mod_engine_; \
  WavetableEngine wavetable_engine_; \
  VowelFofEngine vowel_fof_engine_; \
  PhaseWeaveEngine phase_weave_engine_; \
  BuzzEngine buzz_engine_; \
  VosimEngine vosim_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&virtual_analog_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&fm_engine_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&ring_mod_engine_, false, -1.5f, -1.5f); \
  (registry).RegisterInstance(&wavetable_engine_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&vowel_fof_engine_, false, -3.8f, -3.8f); \
  (registry).RegisterInstance(&phase_weave_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&buzz_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&vosim_engine_, false, -0.8f, -0.8f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
