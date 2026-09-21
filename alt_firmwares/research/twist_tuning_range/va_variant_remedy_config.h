// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// Listening build for the Virtual Analog Variant DEAD CENTRE.
//
// The trajectory's dry point leaves no second oscillator, so every control
// that only shapes the second oscillator goes inert there. That is inherited
// from Crossfade, which measures identically; Dual escapes it only by having
// no trajectory at all. Four candidates side by side, against the three
// engines that exist today.
//
//   AMBER  row 0  Variant, baseline        Crossfade's trajectory verbatim
//          row 1  Variant, LINEAR_FADE     lower half un-squared
//          row 2  Variant, SHAPE_SPREAD    TWIST moves both shapes
//          row 3  Variant, NO_DRY          primary always present
//
//   GREEN  row 0  Virtual Analog           the stock engine, for reference
//          row 1  Virtual Analog Dual      fixed 50/50, no dead centre
//          row 2  Virtual Analog Crossfade the trajectory, dead centre and all
//
// Measured responsiveness of OUT to HARMONICS with the trajectory at its
// midpoint -- RMS change for HARMONICS 0.2 -> 0.8:
//
//   baseline 0.00000   linear 0.00000   spread 0.00000   no-dry 0.37514
//
// and to TWIST at the same point:
//
//   baseline 0.00000   linear 0.00000   spread 0.71880   no-dry 0.41017
//
// So only NO_DRY answers both; SHAPE_SPREAD answers TWIST alone (HARMONICS
// cannot matter where there is no second oscillator to set an interval
// between); LINEAR_FADE answers neither, but roughly halves the mush on the
// approach -- 0.10496 against 0.02099 at 0.40 travel.
//
// Build with:
//   make -f plaits/makefile ENGINE_CONFIG=alt_firmwares/research/twist_tuning_range/va_variant_remedy_config.h

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
#include "plaits/dsp/engine/virtual_analog_dual_engine.h"
#include "plaits/dsp/engine/virtual_analog_crossfade_engine.h"
#include "plaits/dsp/engine/virtual_analog_variant_engine.h"

#define PLAITS_ENGINE_COUNT 7
#define PLAITS_BANK_SIZES { 4, 3 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 0, 1, 2 }

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_CUSTOM_MODEL_DATA 0

// TWIST on the FREQUENCY knob at boot: the shape spread is on it, and on the
// Variant rows it is half of what is being judged.
#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION 1

#define PLAITS_ENGINE_MEMBERS \
  VirtualAnalogVariantEngine variant_baseline_; \
  VirtualAnalogVariantEngine variant_linear_fade_; \
  VirtualAnalogVariantEngine variant_shape_spread_; \
  VirtualAnalogVariantEngine variant_no_dry_; \
  VirtualAnalogEngine virtual_analog_engine_; \
  VirtualAnalogDualEngine virtual_analog_dual_engine_; \
  VirtualAnalogCrossfadeEngine virtual_analog_crossfade_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  variant_baseline_.set_remedy(VARIANT_REMEDY_NONE); \
  variant_linear_fade_.set_remedy(VARIANT_REMEDY_LINEAR_FADE); \
  variant_shape_spread_.set_remedy(VARIANT_REMEDY_SHAPE_SPREAD); \
  variant_no_dry_.set_remedy(VARIANT_REMEDY_NO_DRY); \
  (registry).RegisterInstance(&variant_baseline_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&variant_linear_fade_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&variant_shape_spread_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&variant_no_dry_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_dual_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_crossfade_engine_, false, 0.8f, 0.8f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
