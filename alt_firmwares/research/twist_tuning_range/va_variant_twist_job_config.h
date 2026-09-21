// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// What job should TWIST do on Virtual Analog Variant? One bank, four rows, no
// bank switching -- just the select button.
//
//   AMBER  row 0  Variant, baseline      TWIST = secondary shape, no spread
//          row 1  Variant, SPREAD 0.50   TWIST splays BOTH shapes
//          row 2  Variant, PULSE_WIDTH   TWIST = pulse width, decoupled
//          row 3  Virtual Analog         the stock engine, as the control
//
// Rows 0 and 1 keep the merge: TWIST carries Dual's independent secondary
// shape, so the engine is a superset of both parents. Row 2 spends TWIST on
// pulse width instead, which means the two oscillators share one shape as they
// do in Crossfade -- the engine is then Crossfade plus independent pulse width
// and NOT a superset of Dual. That is the trade being auditioned.
//
// Measured TWIST responsiveness at the dry point (MORPH noon), by TIMBRE:
//
//   TIMBRE      SPREAD 0.50    PULSE_WIDTH
//   0.1             0.09606        0.46706
//   0.3             0.28818        0.06674   <- pulse width's soft spot
//   0.5             0.32887        0.61002
//   0.7             0.50665        1.13594
//   0.9             1.00207        0.66801
//
// Build with:
//   make -f plaits/makefile ENGINE_CONFIG=alt_firmwares/research/twist_tuning_range/va_variant_twist_job_config.h

#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_HAS_TERRAIN_BANK 0
#define PLAITS_WAVE_TERRAIN_FACTORY_MASK 0xff
#define PLAITS_HAS_WAVETABLE_BANK 0
#define PLAITS_WAVETABLE_FACTORY_MASK 0x07
#define PLAITS_HAS_CUSTOM_WAVE_LINES 0
#ifndef PLAITS_BUILD_ENABLE_SYNC_INPUT
#define PLAITS_BUILD_ENABLE_SYNC_INPUT 0
#endif

#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/virtual_analog_variant_engine.h"

#define PLAITS_ENGINE_COUNT 4
#define PLAITS_BANK_SIZES { 4 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3 }

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_CUSTOM_MODEL_DATA 0

#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION 1

#define PLAITS_ENGINE_MEMBERS \
  VirtualAnalogVariantEngine variant_baseline_; \
  VirtualAnalogVariantEngine variant_spread_; \
  VirtualAnalogVariantEngine variant_pulse_width_; \
  VirtualAnalogEngine virtual_analog_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  variant_baseline_.set_remedy(VARIANT_REMEDY_NONE); \
  variant_spread_.set_remedy(VARIANT_REMEDY_SHAPE_SPREAD); \
  variant_spread_.set_spread_primary_ratio(0.50f); \
  variant_pulse_width_.set_remedy(VARIANT_REMEDY_PULSE_WIDTH); \
  (registry).RegisterInstance(&variant_baseline_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&variant_spread_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&variant_pulse_width_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_engine_, false, 0.8f, 0.8f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
