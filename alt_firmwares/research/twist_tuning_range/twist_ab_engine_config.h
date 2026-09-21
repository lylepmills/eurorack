// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// A/B firmware for the TWIST tuning-range question. Hand-written rather than
// generated, because the hosted builder's recipe format addresses engines by
// catalog id and this build deliberately carries the SAME five engines three
// times over, each copy with a different TWIST span.
//
// Registry order is amber, green, red (Ui::BankToColor), and every bank holds
// the same five engines on the same LED rows, so a given row is the same
// engine in all three banks and switching bank is a straight A/B:
//
//   AMBER (rows 0-4)  QUANTIZED  shipped span, snapped to musical steps
//   GREEN (rows 0-4)  STOCK      exactly what ships today
//   RED   (rows 0-4)  NARROW     the same curve over a fine-trim span
//
//   row 0  Two-op FM
//   row 1  Virtual Analog Dual
//   row 2  Virtual Analog Crossfade
//   row 3  Virtual Analog Variant -- GREEN BANK ONLY
//
// Virtual Analog Variant has no TWIST-span variants to compare, so it sits on
// a fourth green row rather than in all three banks: green is then "what ships
// today, plus the engine proposed to replace two of it". Its control map is
// compile-time (PLAITS_VA_VARIANT_TRAJECTORY_ON_TWIST), so a firmware carries
// one arrangement; this build has the default -- trajectory on MORPH, shape
// spread on TWIST.
//
// Phase Distortion and Wave Paraphonic were dropped from the experiment on
// 2026-09-20: Lyle prefers their shipped spans, whose musical endpoints (octave
// up/down; unison and doubled chord) are worth more than the tuning precision
// either alternative would buy. Both engines are byte-identical to master again.
//
// Assign TWIST to the FREQUENCY knob before playing: hold the right button and
// walk the options to "locked frequency pot" = fourth macro. Without that,
// Voice::Render pins macro to exactly 0.5 and all three banks are identical by
// construction -- which is itself worth hearing once as a control.
//
// Build (one line -- a // comment must not end in a backslash, which would
// continue it onto the next line and trip -Werror=comment):
//   make -f plaits/makefile ENGINE_CONFIG=alt_firmwares/research/twist_tuning_range/twist_ab_engine_config.h

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

#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/virtual_analog_dual_engine.h"
#include "plaits/dsp/engine/virtual_analog_crossfade_engine.h"
#include "plaits/dsp/engine/virtual_analog_variant_engine.h"

#define PLAITS_ENGINE_COUNT 10
#define PLAITS_BANK_SIZES { 3, 4, 3 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 0, 1, 2, 3, 0, 1, 2 }

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_CUSTOM_MODEL_DATA 0

// Start with TWIST already on the FREQUENCY knob, so the firmware is playable
// the moment it boots rather than after a menu walk. (1 == fourth synthesis
// macro; see Patch::locked_frequency_pot_option in voice.h.)
#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION 1

#define PLAITS_ENGINE_MEMBERS \
  FMEngine fm_engine_quantized_; \
  VirtualAnalogDualEngine virtual_analog_dual_engine_quantized_; \
  VirtualAnalogCrossfadeEngine virtual_analog_crossfade_engine_quantized_; \
  FMEngine fm_engine_stock_; \
  VirtualAnalogDualEngine virtual_analog_dual_engine_stock_; \
  VirtualAnalogCrossfadeEngine virtual_analog_crossfade_engine_stock_; \
  VirtualAnalogVariantEngine virtual_analog_variant_engine_; \
  FMEngine fm_engine_narrow_; \
  VirtualAnalogDualEngine virtual_analog_dual_engine_narrow_; \
  VirtualAnalogCrossfadeEngine virtual_analog_crossfade_engine_narrow_; \

// The spans are set here, not in Init(): voice.cc runs this macro and only
// afterwards calls Init() on each registered engine, and no engine's Init() or
// Reset() touches twist_tuning_.
#define PLAITS_REGISTER_ENGINES(registry) do { \
  fm_engine_quantized_.set_twist_tuning(TWIST_TUNING_QUANTIZED); \
  virtual_analog_dual_engine_quantized_.set_twist_tuning(TWIST_TUNING_QUANTIZED); \
  virtual_analog_crossfade_engine_quantized_.set_twist_tuning(TWIST_TUNING_QUANTIZED); \
  fm_engine_stock_.set_twist_tuning(TWIST_TUNING_STOCK); \
  virtual_analog_dual_engine_stock_.set_twist_tuning(TWIST_TUNING_STOCK); \
  virtual_analog_crossfade_engine_stock_.set_twist_tuning(TWIST_TUNING_STOCK); \
  fm_engine_narrow_.set_twist_tuning(TWIST_TUNING_NARROW); \
  virtual_analog_dual_engine_narrow_.set_twist_tuning(TWIST_TUNING_NARROW); \
  virtual_analog_crossfade_engine_narrow_.set_twist_tuning(TWIST_TUNING_NARROW); \
  (registry).RegisterInstance(&fm_engine_quantized_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&virtual_analog_dual_engine_quantized_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_crossfade_engine_quantized_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&fm_engine_stock_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&virtual_analog_dual_engine_stock_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_crossfade_engine_stock_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_variant_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&fm_engine_narrow_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&virtual_analog_dual_engine_narrow_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&virtual_analog_crossfade_engine_narrow_, false, 0.8f, 0.8f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
