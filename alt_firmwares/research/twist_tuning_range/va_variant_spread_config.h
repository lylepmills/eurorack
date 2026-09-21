// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// Listening build for SHAPE_SPREAD's follow ratio -- how far TWIST drags the
// PRIMARY along with the secondary.
//
// The benefit and the cost are the same fact: TWIST is audible where only the
// primary sounds precisely BECAUSE it moves the primary. At ratio 1.0 it does
// not spread the shapes, it SWAPS them -- with TIMBRE at noon the primary runs
// 1.000 -> 0.000 as the secondary runs 0.000 -> 1.000, so TIMBRE stops setting
// the primary's shape and sets the midpoint of the pair instead.
//
//   AMBER  row 0  ratio 1.00   TWIST at the dry point: 0.71880
//          row 1  ratio 0.50                           0.32887
//          row 2  ratio 0.33                           0.23080
//          row 3  ratio 0.25                           0.17485
//
//   GREEN  row 0  Variant, baseline    no spread at all -- TWIST dead at noon
//          row 1  Virtual Analog       the stock engine
//          row 2  Virtual Analog Dual  fixed 50/50, no trajectory
//          row 3  Virtual Analog Xfade the trajectory, dead centre and all
//
// For scale: TWIST's ordinary effect with the secondary present measures
// 0.48381, so ratio 0.33 leaves it about half as strong at the dry point.
//
// Build with:
//   make -f plaits/makefile ENGINE_CONFIG=alt_firmwares/research/twist_tuning_range/va_variant_spread_config.h

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

#define PLAITS_ENGINE_COUNT 8
#define PLAITS_BANK_SIZES { 4, 4 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 0, 1, 2, 3 }

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_CUSTOM_MODEL_DATA 0

#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION 1

#define PLAITS_ENGINE_MEMBERS \
  VirtualAnalogVariantEngine spread_100_; \
  VirtualAnalogVariantEngine spread_050_; \
  VirtualAnalogVariantEngine spread_033_; \
  VirtualAnalogVariantEngine spread_025_; \
  VirtualAnalogVariantEngine variant_baseline_; \
  VirtualAnalogEngine virtual_analog_engine_; \
  VirtualAnalogDualEngine virtual_analog_dual_engine_; \
  VirtualAnalogCrossfadeEngine virtual_analog_crossfade_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  spread_100_.set_remedy(VARIANT_REMEDY_SHAPE_SPREAD); \
  spread_100_.set_spread_primary_ratio(1.00f); \
  spread_050_.set_remedy(VARIANT_REMEDY_SHAPE_SPREAD); \
  spread_050_.set_spread_primary_ratio(0.50f); \
  spread_033_.set_remedy(VARIANT_REMEDY_SHAPE_SPREAD); \
  spread_033_.set_spread_primary_ratio(0.33f); \
  spread_025_.set_remedy(VARIANT_REMEDY_SHAPE_SPREAD); \
  spread_025_.set_spread_primary_ratio(0.25f); \
  variant_baseline_.set_remedy(VARIANT_REMEDY_NONE); \
  (registry).RegisterInstance(&spread_100_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&spread_050_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&spread_033_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&spread_025_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&variant_baseline_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_dual_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_crossfade_engine_, false, 0.8f, 0.8f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
