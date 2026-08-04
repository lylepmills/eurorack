// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// UI and CV processing ("controller" and "view")

#include "plaits/ui.h"

#include "plaits/cpu_probe.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/system/system_clock.h"
#include "plaits/alternate_parameter_leds.h"
#include "plaits/build_config.h"
#include "plaits/bank_navigation.h"

namespace plaits {

using namespace std;
using namespace stmlib;

static const int32_t kLongPressTime = 2000;

// Ui::Poll runs once per 12-sample audio block, or 4,000 times per second.
// Waiting two seconds after the last quantized pitch change coalesces an entire
// tuning gesture into one flash write.
static const uint16_t kPrecisionSaveDelay = 8000;

// The build's bank layout, baked in by the generated config (build_config.h).
// bank_navigation.h reads bank/row from this table; the LED display and
// Navigate() use it instead of assuming eight-wide banks. The firmware is built
// as C++98 (arm-none-eabi 4.8), so this is a plain const table — no
// constexpr/static_assert. The invariants (the banks sum to PLAITS_ENGINE_COUNT,
// each holds at most eight, one to four banks) are enforced by the config
// generator and its tests (alt_firmwares/plaits_lab_builder), and build_config.h
// #errors on a bad PLAITS_ENGINE_COUNT. kNumBanks is an integral constant
// expression, so BankToColor's `kNumBanks > 3` still folds away for 3-bank
// builds.
static const uint8_t kBankSizes[] = PLAITS_BANK_SIZES;
static const int kNumBanks = sizeof(kBankSizes) / sizeof(kBankSizes[0]);

// Physical LED row (0..7) of each engine within its bank, indexed by the flat,
// compact engine index. Engine navigation stays compact — the select button
// cycles the bank's real engines and MODEL CV maps across them — but the LED
// lights each engine's TRUE row, so a bank with gaps (a deleted mid-bank model)
// shows its engines at their kept positions rather than slid to the front. For
// a fully-packed bank this is the identity (row == compact row) and behaves
// exactly as before. See PLAITS_ENGINE_ROWS in build_config.h.
static const uint8_t kEngineRows[] = PLAITS_ENGINE_ROWS;

// The options menu, in light order: the lights the player reaches for most sit
// closest to the entry point, since the left button only walks forward. Two
// chains below are indexed by this (the LED render and the value cycler), so
// they are named rather than numbered - a silent disagreement between them
// would be the whole hazard of reordering.
enum OptionLight {
  OPTION_LIGHT_CHORD_SET = 0,
  OPTION_LIGHT_AUX_OUTPUT,
  OPTION_LIGHT_SUBOSC,
  OPTION_LIGHT_FREQUENCY_POT,
  OPTION_LIGHT_MODEL_CV,
  OPTION_LIGHT_LEVEL_CV,
  OPTION_LIGHT_HOLD_ON_TRIGGER,
  OPTION_LIGHT_ATTENUVERTER_MODE,
  OPTION_LIGHT_LAST
};

static const uint8_t kNumOptions = OPTION_LIGHT_LAST;
static const uint8_t kNumLockedFrequencyPotOptions = 4;
static const uint8_t kNumModelCVOptions = 4;
static const uint8_t kNumLevelCVOptions = 3;
// Aux output: regular aux model, stereo (true L/R pair on stereo-capable
// engines), suboscillator. The suboscillator's own light then carries shape
// and octave together - square/sine crossed with 0, -1 and -2 octaves.
static const uint8_t kNumAuxOutputOptions = 3;
static const uint8_t kNumSuboscOptions = 6;
static const uint8_t kNumChordSetOptions = PLAITS_CHORD_TABLE_COUNT;
static const uint8_t kNumHoldOnTriggerOptions = 2;
static const uint8_t kNumAttenuverterModes = ATTENUVERTER_MODE_LAST;

// Steps an option by delta, wrapping at either end. delta is +/-1; the Ro'Ved
// panel is the only caller that passes -1.
static int CycleOption(int value, int num_values, int delta) {
  int result = value + delta;
  if (result < 0) {
    result = num_values - 1;
  } else if (result >= num_values) {
    result = 0;
  }
  return result;
}

void Ui::Init(Patch* patch, Modulations* modulations, Settings* settings) {
  patch_ = patch;
  modulations_ = modulations;
  settings_ = settings;

  cv_adc_.Init();
  pots_adc_.Init();
  leds_.Init();
  switches_.Init();

  ui_task_ = 0;
  option_index_ = 0;
  mode_ = UI_MODE_NORMAL;

  octave_quantizer_.Init(9, 0.01f, false);

  LoadState();

  // Bind pots to parameters.
  pots_[POTS_ADC_CHANNEL_FREQUENCY_POT].Init(
      &transposition_, NULL, 0.005f, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Init(
      &patch->harmonics, &octave_, 0.005f, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Init(
      &patch->timbre, &patch->lpg_colour, 0.01f, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_MORPH_POT].Init(
      &patch->morph, &patch->decay, 0.01f, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_TIMBRE_ATTENUVERTER].Init(
      &patch->timbre_modulation_amount, NULL, 0.005f, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Init(
      &patch->frequency_modulation_amount, NULL, 0.005f, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_MORPH_ATTENUVERTER].Init(
      &patch->morph_modulation_amount, NULL, 0.005f, 2.0f, -1.0f);

  // Keep track of the agreement between the random sequence sent to the
  // switch and the value read by the ADC.
  normalization_detection_count_ = 0;
  normalization_probe_state_ = 0;
  normalization_probe_.Init();
  fill(
      &normalization_detection_mismatches_[0],
      &normalization_detection_mismatches_[5],
      0);

  pwm_counter_ = 0;
  fill(&press_time_[0], &press_time_[SWITCH_LAST], 0);
  fill(&ignore_release_[0], &ignore_release_[SWITCH_LAST], false);

  active_engine_ = 0;
  pitch_lp_ = 0.0f;
  data_transfer_progress_ = 0.0f;

  // LoadState() restored locked_octave_. Keep its continuous counterpart ready
  // for the right-button + MORPH shortcut without overwriting the saved octave.
  locked_octave_control_ = static_cast<float>(locked_octave_) / 8.0f;
  locked_octave_gesture_armed_ = false;
  editing_locked_octave_ = false;

  previous_pitch_range_ = PitchRangeFromControl(octave_);
  precision_anchor_note_ = tuned_root_note_;
  precision_catch_up_.Reset();
  octave_catch_up_.Reset();
  precision_root_save_.Init(
      EncodeTunedRoot(tuned_root_note_), kPrecisionSaveDelay);

#if PLAITS_BUILD_ENABLE_CALIBRATION
  // Calibration state is initialized under the gate along with everything else
  // that touches it, so a build without the procedure emits no code for it at
  // all and stays byte-identical to one built before this existed.
  pitch_lp_calibration_ = 0.0f;
  cv_c1_ = 0.0f;
  calibration_step_ = 0;
  calibration_armed_ = false;

  // Holding the RIGHT button through power-up starts calibration — the mirror
  // of the bootloader's left-button firmware-update gesture (bootloader.cc
  // checks Switch(0); this is Switch(1)). On Ro'Ved, Switch(1) is the MORPH
  // knob click. Both variants therefore use an intentional held-at-power-up
  // gesture. Read straight off the GPIO: the debouncer has no history this
  // early.
  if (switches_.pressed_immediate(SWITCH_ROW_2)) {
    StartCalibration();
  }
#endif  // PLAITS_BUILD_ENABLE_CALIBRATION
#if PLAITS_CPU_PROBE
  cpu_usage_ = 0.0f;
#endif
}

// An option light whose setting cannot do anything right now. The suboscillator
// shape and octave only matter while the aux output IS a suboscillator; rather
// than show a light that does nothing, the menu darkens it and walks past it.
bool Ui::OptionInert(int index) const {
  return index == OPTION_LIGHT_SUBOSC && !patch_->aux_is_subosc();
}

// Step to the next light the player can actually use in either direction. The
// loop is bounded so that a future second inert option can never spin here;
// the chord light is never inert, so it always terminates well before the
// bound. Stock Plaits only passes +1; Ro'Ved exposes both directions.
void Ui::StepOptionIndex(int delta) {
  for (int n = 0; n < kNumOptions; ++n) {
    option_index_ = CycleOption(option_index_, kNumOptions, delta);
    if (!OptionInert(option_index_)) {
      break;
    }
  }
}

void Ui::LoadState() {
  const State& state = settings_->state();
  patch_->engine = state.engine & 0x1f;
  patch_->attenuverter_mode = (state.engine >> 5) & 0x03;
  patch_->lpg_colour = static_cast<float>(state.lpg_colour) / 256.0f;
  patch_->decay = static_cast<float>(state.decay) / 256.0f;
  octave_ = static_cast<float>(state.octave) / 256.0f;
  fine_tune_ = static_cast<float>(state.fine_tune) / 256.0f;

  // alt firmware
  patch_->locked_frequency_pot_option = state.locked_frequency_pot_option;
  patch_->model_cv_option = state.model_cv_option;
  patch_->level_cv_option = state.level_cv_option;
  patch_->aux_output_option = state.aux_output_option;
  patch_->aux_subosc_option = state.aux_subosc_option;
  patch_->chord_set_option = state.chord_set_option;
  patch_->hold_on_trigger_option = state.hold_on_trigger_option;
  locked_octave_ = state.locked_octave;
  tuned_root_note_ = state.tuned_root_valid == 1
      ? DecodeTunedRoot(state.tuned_root_q8)
      : 60.0f;

  for (int i = 0; i < 4; ++i) {
    bank_last_row_[i] = state.bank_last_row[i];
  }
  // Keep the current bank's remembered row consistent with the restored engine
  // (e.g. if the engine was clamped after a rebuild changed the bank sizes).
  bank_last_row_[BankOfEngine(kBankSizes, kNumBanks, patch_->engine)] =
      static_cast<uint8_t>(RowOfEngine(kBankSizes, kNumBanks, patch_->engine));
}

void Ui::SaveState() {
  State* state = settings_->mutable_state();
  state->engine = static_cast<uint8_t>(
      (patch_->engine & 0x1f) | (patch_->attenuverter_mode << 5));
  state->lpg_colour = static_cast<uint8_t>(patch_->lpg_colour * 256.0f);
  state->decay = static_cast<uint8_t>(patch_->decay * 256.0f);
  state->octave = static_cast<uint8_t>(octave_ * 256.0f);
  state->fine_tune = static_cast<uint8_t>(fine_tune_ * 256.0f);
  // Retire the unfinished one-sided FM-attenuverter trim from the legacy state.
  // Zero is its neutral value in the old arithmetic and prevents a fresh
  // state from shifting every locked range sharp by half a semitone.
  state->extra_fine_tune = 0;

  // alt firmware
  state->locked_frequency_pot_option = patch_->locked_frequency_pot_option;
  state->model_cv_option = patch_->model_cv_option;
  state->level_cv_option = patch_->level_cv_option;
  state->aux_output_option = patch_->aux_output_option;
  state->aux_subosc_option = patch_->aux_subosc_option;
  state->chord_set_option = patch_->chord_set_option;
  state->hold_on_trigger_option = patch_->hold_on_trigger_option;
  state->locked_octave = locked_octave_;
  state->tuned_root_q8 = EncodeTunedRoot(tuned_root_note_);
  state->tuned_root_valid = 1;

  for (int i = 0; i < 4; ++i) {
    state->bank_last_row[i] = bank_last_row_[i];
  }

  settings_->SaveState();
  precision_root_save_.MarkSaved(EncodeTunedRoot(tuned_root_note_));
}

uint32_t Ui::BankToColor(int bank) {
#if PLAITS_BUILD_COLOR_BLIND_MODE
  // One hue, four brightness levels. The generated registry is ordered amber,
  // green, red for three banks, and orange, green, red, amber for four. Encode
  // the PUBLIC order from brightest to faintest so the field guide and editor
  // can describe one stable sequence: green 100%, red 50%, amber 25%, orange
  // 12.5%. Sixteen-phase PWM keeps every level flicker-free and leaves the
  // slower selected-model pulse as a separate visual signal.
  static const uint8_t kThreeBankBrightnessDuty[3] = { 4, 16, 8 };
  static const uint8_t kFourBankBrightnessDuty[4] = { 2, 16, 8, 4 };
  const uint8_t duty = kNumBanks > 3
      ? kFourBankBrightnessDuty[bank]
      : kThreeBankBrightnessDuty[bank];
  return (pwm_counter_ & 15) < duty
      ? LED_COLOR_YELLOW
      : LED_COLOR_OFF;
#else
  // kNumBanks is a compile-time constant, so for three-bank builds this branch
  // folds away (equivalent to the previous #if PLAITS_ENGINE_COUNT > 24).
  if (kNumBanks > 3 && bank == 0) {
    // Fourth bank: orange. The LED channels are 1-bit (drivers/leds.cc), so
    // blend red with quarter-duty green by dithering at the poll rate — far
    // above flicker fusion, it reads as a steady color between red and the
    // full-duty yellow of the amber bank.
    return (pwm_counter_ & 3) ? LED_COLOR_RED : LED_COLOR_YELLOW;
  }
  static const uint32_t kThreeBankColors[3] = {
    LED_COLOR_YELLOW, LED_COLOR_GREEN, LED_COLOR_RED
  };
  static const uint32_t kFourBankColors[4] = {
    LED_COLOR_OFF, LED_COLOR_GREEN, LED_COLOR_RED, LED_COLOR_YELLOW
  };
  return kNumBanks > 3 ? kFourBankColors[bank] : kThreeBankColors[bank];
#endif
}

void Ui::UpdateLEDs() {
  leds_.Clear();
  ++pwm_counter_;

#if PLAITS_CPU_PROBE && PLAITS_CPU_PROBE_LEDS
  // A probe build's LEDs are a CPU meter, so an engine's cost is readable with
  // nothing patched at all and no output given up -- which is why this is the
  // channel a contributor build ships. (PLAITS_CPU_PROBE_AUX adds the precise
  // tone readout, at the cost of the engine's own AUX output.)
  // Each LED is one eighth of the audio callback's budget: lit green while the
  // engine fits, amber for the last quarter, and all eight blinking red once it
  // is over budget (the blink gets faster the further over it is).
  //
  // ONLY in the normal (engine-select) view. That display is the one a probe
  // build can spare -- these are one-model firmwares, so there is nothing to
  // select -- while every other screen still has work to do: a contributor has
  // to be able to set the octave, dial in LPG colour and decay, and walk the
  // options page to reach the very settings an engine may depend on. Taking
  // those over would make the probe build untestable.
  if (mode_ == UI_MODE_NORMAL) {
    const float usage = cpu_usage_;
    // The red line sits at 90%, not 100%: this meter brackets Voice::Render
    // only, and the UI poll, watchdog and readout ride the same interrupt
    // OUTSIDE the bracket. Hardware-calibrated: an engine reading 87-100%
    // here (4-octave Helix) audibly crunches, while 62-75% (3-octave) has
    // real margin. Above 90% the deadline is at risk in practice.
    if (usage > 0.9f) {
      // Deadline at risk: blink everything red, faster the worse it is.
      const int shift = usage > 1.8f ? 5 : (usage > 1.2f ? 6 : 7);
      if ((pwm_counter_ >> shift) & 1) {
        for (int i = 0; i < kNumLEDs; ++i) {
          leds_.set(i, LED_COLOR_RED);
        }
      }
    } else {
      // Fills bottom-up; colour keys off the LEVEL (i), not the physical LED,
      // so the top two eighths of the scale are always the amber ones. The
      // first PARTIAL eighth shows as the next LED's brightness: 16-level PWM
      // on the fractional part, so the bar reads to ~1% instead of in eight
      // discrete steps.
      const float scaled = usage * static_cast<float>(kNumLEDs);
      int full = static_cast<int>(scaled);
      if (full > kNumLEDs) full = kNumLEDs;
      // Amber from 62.5% up: the top three eighths are where un-bracketed
      // ISR overhead starts to matter.
      for (int i = 0; i < full; ++i) {
        leds_.set(kNumLEDs - 1 - i,
                  i >= kNumLEDs - 3 ? LED_COLOR_YELLOW : LED_COLOR_GREEN);
      }
      if (full < kNumLEDs) {
        const float frac = scaled - static_cast<float>(full);
        if (static_cast<int>(frac * 16.0f) > static_cast<int>(pwm_counter_ & 15)) {
          leds_.set(kNumLEDs - 1 - full,
                    full >= kNumLEDs - 3 ? LED_COLOR_YELLOW : LED_COLOR_GREEN);
        }
      }
    }
    leds_.Write();
    return;
  }
#endif  // PLAITS_CPU_PROBE && PLAITS_CPU_PROBE_LEDS

  int pwm_counter = pwm_counter_ & 15;
  int triangle = (pwm_counter_ >> 4) & 31;
  triangle = triangle < 16 ? triangle : 31 - triangle;

#if PLAITS_BUILD_ENABLE_CALIBRATION
  // Calibrating takes over the display entirely, like the probe meter above:
  // the first light pulses green while the module waits for the low note's
  // voltage and yellow for the high one. This is the stock firmware's display,
  // so Mutable's published calibration instructions read correctly against a
  // build that includes the procedure.
  if (calibration_step_) {
    if (pwm_counter < triangle) {
#if PLAITS_BUILD_COLOR_BLIND_MODE
      // In an accessible build the first step is brightest and the second is
      // dim. The outer triangle remains the slow "waiting" pulse.
      if (calibration_step_ == 1 || !(pwm_counter_ & 3)) {
        leds_.set(0, LED_COLOR_YELLOW);
      }
#else
      leds_.set(0, calibration_step_ == 1 ? LED_COLOR_GREEN : LED_COLOR_YELLOW);
#endif
    }
    leds_.Write();
    return;
  }
#endif  // PLAITS_BUILD_ENABLE_CALIBRATION

  switch (mode_) {
    case UI_MODE_NORMAL:
      {
        // Selected with the buttons. The lit LED is the engine's PHYSICAL row
        // (kEngineRows), so an engine in a gapped bank stays on its own LED
        // rather than the compacted position; the bank (hence color) is still
        // derived from the compact index.
        const int selected_bank =
            BankOfEngine(kBankSizes, kNumBanks, patch_->engine);
        const int selected_row = kEngineRows[patch_->engine];
        uint32_t selected_color = pwm_counter < triangle
            ? BankToColor(selected_bank)
            : LED_COLOR_OFF;

        // With the CV modulation applied
        const int active_bank =
            BankOfEngine(kBankSizes, kNumBanks, active_engine_);
        const int active_row = kEngineRows[active_engine_];
        uint32_t active_color = BankToColor(active_bank);

        leds_.set(active_row, active_color);
        leds_.mask(selected_row, selected_color);
      }
      break;

    case UI_MODE_DISPLAY_ALTERNATE_PARAMETERS:
      {
        for (int parameter = 0; parameter < 2; ++parameter) {
          float value = parameter == 0
              ? patch_->lpg_colour
              : patch_->decay;
          value -= 0.001f;
          for (int i = 0; i < 4; ++i) {
            leds_.set(
                AlternateParameterLedIndex(
                    parameter, i, PLAITS_ROVED_PANEL != 0),
                value * 64.0f > pwm_counter ? LED_COLOR_YELLOW : LED_COLOR_OFF);
            value -= 0.25f;
          }
        }
      }
      break;

    case UI_MODE_DISPLAY_DATA_TRANSFER_PROGRESS:
      {
        if (data_transfer_progress_ == 1.0f) {
          for (int i = 0; i < 8; ++i) {
            leds_.set(
                i, i == (triangle >> 1) ? LED_COLOR_OFF : LED_COLOR_GREEN);
          }
        } else if (data_transfer_progress_ < 0.0f) {
          for (int i = 0; i < 8; ++i) {
            leds_.set(
                i, pwm_counter < triangle ? LED_COLOR_RED : LED_COLOR_OFF);
          }
        } else {
          float value = data_transfer_progress_ - 0.001f;
          for (int i = 0; i < 8; ++i) {
            leds_.set(i, value * 128.0f > pwm_counter
                ? LED_COLOR_GREEN : LED_COLOR_OFF);
            value -= 0.125f;
          }
        }
      }
      if (pwm_counter_ > 3000) {
        mode_ = UI_MODE_NORMAL;
      }
      break;

    case UI_MODE_DISPLAY_OCTAVE:
      {
        // The right-button + MORPH shortcut has nine octave choices but only
        // eight model lights. The first eight choices climb one light at a
        // time; all lights indicate the ninth/highest octave. Existing hidden
        // octave/range editing keeps its original display.
        const int octave = editing_locked_octave_
            ? (locked_octave_ == 8 ? PITCH_RANGE_HIGH : locked_octave_ + 1)
            : PitchRangeFromControl(octave_);
        for (int i = 0; i < 8; ++i) {
          LedColor color = LED_COLOR_OFF;
          if (octave == 0) {
            color = i == (triangle >> 1) ? LED_COLOR_OFF : LED_COLOR_YELLOW;
          } else if (octave == PITCH_RANGE_HIGH) {
            color = LED_COLOR_YELLOW;
          } else if (octave == PITCH_RANGE_OCTAVES) {
            color = (i & 1) == ((triangle >> 3) & 1)
                ? LED_COLOR_OFF
                : LED_COLOR_YELLOW;
          } else if (octave == PITCH_RANGE_PRECISION) {
            color = pwm_counter < triangle ? LED_COLOR_YELLOW : LED_COLOR_OFF;
          } else {
            color = (octave - 1) == i ? LED_COLOR_YELLOW : LED_COLOR_OFF;
          }
          leds_.set(7 - i, color);
        }
      }
      break;

    case UI_MODE_CHANGE_OPTIONS_PRE_RELEASE:
    case UI_MODE_CHANGE_OPTIONS:
      for (int i = 0; i < kNumOptions; ++i) {
        // An option that does nothing under the current settings goes dark and
        // is skipped by the navigation below, so the menu is only ever as long
        // as the settings it can actually act on.
        if (OptionInert(i)) {
          leds_.set(i, LED_COLOR_OFF);
          continue;
        }

        int option_value = 0;
        if (i == OPTION_LIGHT_CHORD_SET) {
          option_value = patch_->chord_set_option;
        } else if (i == OPTION_LIGHT_AUX_OUTPUT) {
          option_value = patch_->aux_output_option;
        } else if (i == OPTION_LIGHT_SUBOSC) {
          option_value = patch_->aux_subosc_option;
        } else if (i == OPTION_LIGHT_FREQUENCY_POT) {
          option_value = patch_->locked_frequency_pot_option;
        } else if (i == OPTION_LIGHT_MODEL_CV) {
          option_value = patch_->model_cv_option;
        } else if (i == OPTION_LIGHT_LEVEL_CV) {
          option_value = patch_->level_cv_option;
        } else if (i == OPTION_LIGHT_HOLD_ON_TRIGGER) {
          option_value = patch_->hold_on_trigger_option;
        } else if (i == OPTION_LIGHT_ATTENUVERTER_MODE) {
          option_value = patch_->attenuverter_mode;
        }

        // Each option value encodes as one of three appearances crossed with a
        // blink tier: solid (0-2), slow blink (3-5), fast blink (6-8). Normal
        // builds use green/red/yellow; accessible builds use brightest/medium/
        // dim yellow PWM so the setting never depends on hue. Only
        // chord_set_option ranges past 5.
        LedColor color = LED_COLOR_OFF;
#if PLAITS_BUILD_COLOR_BLIND_MODE
        static const uint8_t kOptionBrightnessDuty[3] = { 16, 8, 4 };
        if ((pwm_counter_ & 15) < kOptionBrightnessDuty[option_value % 3]) {
          color = LED_COLOR_YELLOW;
        }
#else
        switch (option_value % 3) {
          case 0: color = LED_COLOR_GREEN; break;
          case 1: color = LED_COLOR_RED; break;
          case 2: color = LED_COLOR_YELLOW; break;
        }
#endif
        const int blink_tier = option_value / 3;  // 0 solid, 1 slow, 2 fast
        if (blink_tier == 1 && (pwm_counter_ & 128)) {
          color = LED_COLOR_OFF;  // slow blink (values 3-5)
        } else if (blink_tier == 2 && (pwm_counter_ & 64)) {
          color = LED_COLOR_OFF;  // fast blink (values 6-8), 2x the slow rate
        }

        // Dim the other lights
        if ((i != option_index_) && (pwm_counter & 7)) {
            color = LED_COLOR_OFF;
        }

        leds_.set(i, color);
      }
      break;

    case UI_MODE_ERROR:
      if (pwm_counter < triangle) {
        for (int i = 0; i < kNumLEDs; ++i) {
          leds_.set(i, LED_COLOR_RED);
        }
      }
      break;

    case UI_MODE_TEST:
      {
        int color = (pwm_counter_ >> 10) % 3;
        for (int i = 0; i < kNumLEDs; ++i) {
          leds_.set(
              i, pwm_counter > ((triangle + (i * 2)) & 15)
                  ? (color == 0
                     ? LED_COLOR_GREEN
                      : (color == 1 ? LED_COLOR_YELLOW : LED_COLOR_RED))
                  : LED_COLOR_OFF);
        }
      }
      break;

  }
  leds_.Write();
}

void Ui::Navigate(int button) {
  for (int i = 0; i < SWITCH_LAST; ++i) {
    ignore_release_[i] = true;
  }
  RealignPots();

#if PLAITS_ROVED_PANEL
  // Four clickable knobs give both traversals at once, so the build-time
  // navigation mode is not consulted. In physical left-to-right order,
  // FREQUENCY/TIMBRE change bank and MORPH/HARMONICS step one engine globally.
  // The left knob in each pair goes backward and the right knob goes forward.
  // Bank changes use the same per-bank memory as stock Plaits and work with
  // short, empty, sparse, and fourth banks.
  if (button == 0 || button == 3) {
    const int direction = button == 0 ? 1 : -1;
    patch_->engine = ChangeBank(
        kBankSizes, kNumBanks, patch_->engine, bank_last_row_, direction);
  } else {
    const uint8_t increment =
        button == 2 ? 1 : PLAITS_ENGINE_COUNT - 1;
    patch_->engine =
        (patch_->engine + increment) % PLAITS_ENGINE_COUNT;
  }
#else  // PLAITS_ROVED_PANEL
  if (PLAITS_BUILD_NAVIGATION_MODE == 1) {
    // Banked: button 0 changes bank (landing on that bank's remembered row —
    // per-bank memory), button 1 steps within the current bank, wrapping after
    // its real last engine so a short bank cycles only its engines.
    patch_->engine = button == 0
        ? ChangeBank(kBankSizes, kNumBanks, patch_->engine, bank_last_row_)
        : StepWithinBank(kBankSizes, kNumBanks, patch_->engine);
  } else {
    // Linear: step through every engine, wrapping at the total count.
    const uint8_t increment = button == 0 ? PLAITS_ENGINE_COUNT - 1 : 1;
    patch_->engine = (patch_->engine + increment) % PLAITS_ENGINE_COUNT;
  }
#endif  // PLAITS_ROVED_PANEL

  // Remember the row within the bank we're now in, so a later change-bank can
  // restore it.
  bank_last_row_[BankOfEngine(kBankSizes, kNumBanks, patch_->engine)] =
      static_cast<uint8_t>(RowOfEngine(kBankSizes, kNumBanks, patch_->engine));

  SaveState();
}

void Ui::ReadSwitches() {
  switches_.Debounce();

#if PLAITS_BUILD_ENABLE_CALIBRATION
  if (calibration_step_) {
    // The button that started this is still held down at power-up, and the
    // debouncer reports it as a fresh press a few milliseconds in — which would
    // capture the first voltage before the player has patched anything. So the
    // steps stay disarmed until both buttons are physically up. pressed() needs
    // eight consecutive samples of history the debouncer does not have yet this
    // early, so arm off the raw GPIO instead.
    if (!calibration_armed_) {
      if (!switches_.pressed_immediate(SWITCH_ROW_1) &&
          !switches_.pressed_immediate(SWITCH_ROW_2)) {
        calibration_armed_ = true;
      }
      return;
    }
    for (int i = 0; i < SWITCH_LAST; ++i) {
      if (switches_.just_pressed(Switch(i))) {
        press_time_[i] = 0;
        ignore_release_[i] = true;
        if (calibration_step_ == 1) {
          CalibrateC1();
        } else {
          CalibrateC3();
        }
        break;
      }
    }
    return;
  }
#endif  // PLAITS_BUILD_ENABLE_CALIBRATION

  switch (mode_) {
    case UI_MODE_NORMAL:
      {
#if PLAITS_ROVED_PANEL
        // TIMBRE + FREQUENCY enters the options menu.
        if ((switches_.just_pressed(Switch(0)) && switches_.pressed(Switch(3))) ||
            (switches_.just_pressed(Switch(3)) && switches_.pressed(Switch(0)))) {
          if (OptionInert(option_index_)) {
            StepOptionIndex(1);
          }
          mode_ = UI_MODE_CHANGE_OPTIONS_PRE_RELEASE;
          break;
        }
#else  // PLAITS_ROVED_PANEL
        // Press both buttons to enter options menu
        if ((switches_.just_pressed(Switch(0)) && switches_.pressed(Switch(1))) ||
            (switches_.just_pressed(Switch(1)) && switches_.pressed(Switch(0)))) {
          // option_index_ survives between menu visits, so it can be parked on
          // a light that has since gone inert. Land on a usable one.
          if (OptionInert(option_index_)) {
            StepOptionIndex(1);
          }
          mode_ = UI_MODE_CHANGE_OPTIONS_PRE_RELEASE;
          break;
        }
#endif  // PLAITS_ROVED_PANEL

        for (int i = 0; i < SWITCH_LAST; ++i) {
          if (switches_.just_pressed(Switch(i))) {
            press_time_[i] = 0;
            ignore_release_[i] = false;
          }
          if (switches_.pressed(Switch(i))) {
            ++press_time_[i];
          } else {
            press_time_[i] = 0;
          }
        }

#if PLAITS_ROVED_PANEL
        // Each click locks its own knob; FREQUENCY is not locked on press
        // because its click is the backward-navigation control.
        if (switches_.just_pressed(Switch(0))) {
          pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Lock();
        }
        if (switches_.just_pressed(Switch(1))) {
          pots_[POTS_ADC_CHANNEL_MORPH_POT].Lock();
        }
        if (switches_.just_pressed(Switch(2))) {
          pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Lock();
        }

        if (pots_[POTS_ADC_CHANNEL_MORPH_POT].editing_hidden_parameter() ||
            pots_[POTS_ADC_CHANNEL_TIMBRE_POT].editing_hidden_parameter()) {
          mode_ = UI_MODE_DISPLAY_ALTERNATE_PARAMETERS;
        }

        if (pots_[POTS_ADC_CHANNEL_HARMONICS_POT].editing_hidden_parameter()) {
          mode_ = UI_MODE_DISPLAY_OCTAVE;
        }

        // Long press: display the value of the hidden parameters.
        if (press_time_[0] >= kLongPressTime || press_time_[1] >= kLongPressTime) {
          fill(&press_time_[0], &press_time_[SWITCH_LAST], 0);
          mode_ = UI_MODE_DISPLAY_ALTERNATE_PARAMETERS;
        }
        if (press_time_[2] >= kLongPressTime) {
          fill(&press_time_[0], &press_time_[SWITCH_LAST], 0);
          mode_ = UI_MODE_DISPLAY_OCTAVE;
        }

        if (switches_.released(Switch(3)) && !ignore_release_[3]) {
          Navigate(3);
        } else if (switches_.released(Switch(0)) && !ignore_release_[0]) {
          Navigate(0);
        } else if (switches_.released(Switch(1)) && !ignore_release_[1]) {
          Navigate(1);
        } else if (switches_.released(Switch(2)) && !ignore_release_[2]) {
          Navigate(2);
        }
#else  // PLAITS_ROVED_PANEL
        if (switches_.just_pressed(Switch(0))) {
          pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Lock();
          pots_[POTS_ADC_CHANNEL_MORPH_POT].Lock();
        }
        if (switches_.just_pressed(Switch(1))) {
          pots_[POTS_ADC_CHANNEL_FREQUENCY_POT].Lock();
          pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Lock();
          pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Lock();
          // Once FREQUENCY is assigned to the fourth macro, aux crossfade, or
          // decay, preserve octave access with right button + MORPH. The
          // dynamic hidden target freezes MORPH itself and restores its normal
          // decay binding on release.
          if (patch_->locked_frequency_pot_option != 0) {
            locked_octave_control_ =
                static_cast<float>(locked_octave_) / 8.0f;
            pots_[POTS_ADC_CHANNEL_MORPH_POT].Lock(
                &locked_octave_control_);
            locked_octave_gesture_armed_ = true;
          }
        }

        if ((pots_[POTS_ADC_CHANNEL_MORPH_POT].editing_hidden_parameter() &&
             !editing_locked_octave_) ||
            pots_[POTS_ADC_CHANNEL_TIMBRE_POT].editing_hidden_parameter()) {
          mode_ = UI_MODE_DISPLAY_ALTERNATE_PARAMETERS;
        }

        if ((pots_[POTS_ADC_CHANNEL_MORPH_POT].editing_hidden_parameter() &&
             editing_locked_octave_) ||
            pots_[POTS_ADC_CHANNEL_HARMONICS_POT].editing_hidden_parameter() ||
            pots_[POTS_ADC_CHANNEL_FREQUENCY_POT].editing_hidden_parameter() ||
            pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].editing_hidden_parameter()) {
          mode_ = UI_MODE_DISPLAY_OCTAVE;
        }

        // Long press or actually editing any hidden parameter: display value
        // of hidden parameters.
        if (press_time_[0] >= kLongPressTime && !press_time_[1]) {
          press_time_[0] = press_time_[1] = 0;
          mode_ = UI_MODE_DISPLAY_ALTERNATE_PARAMETERS;
        }
        if (press_time_[1] >= kLongPressTime && !press_time_[0]) {
          press_time_[0] = press_time_[1] = 0;
          mode_ = UI_MODE_DISPLAY_OCTAVE;
        }

        if (switches_.released(Switch(0)) && !ignore_release_[0]) {
          Navigate(0);
        } else if (switches_.released(Switch(1)) && !ignore_release_[1]) {
          locked_octave_gesture_armed_ = false;
          editing_locked_octave_ = false;
          Navigate(1);
        }
#endif  // PLAITS_ROVED_PANEL
      }
      break;

    case UI_MODE_DISPLAY_ALTERNATE_PARAMETERS:
    case UI_MODE_DISPLAY_OCTAVE:
      for (int i = 0; i < SWITCH_LAST; ++i) {
        if (switches_.released(Switch(i))) {
          const bool save_locked_octave = editing_locked_octave_;
          pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_MORPH_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_FREQUENCY_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Unlock();
          locked_octave_gesture_armed_ = false;
          editing_locked_octave_ = false;
          press_time_[i] = 0;
          mode_ = UI_MODE_NORMAL;
          if (save_locked_octave) {
            SaveState();
          }
        }
      }
      break;

    case UI_MODE_DISPLAY_DATA_TRANSFER_PROGRESS:
      break;

    case UI_MODE_CHANGE_OPTIONS_PRE_RELEASE:
#if PLAITS_ROVED_PANEL
      if ((!switches_.pressed(Switch(0)) && !switches_.pressed(Switch(3))) &&
          (switches_.released(Switch(0)) || switches_.released(Switch(3)))) {
#else  // PLAITS_ROVED_PANEL
      if ((!switches_.pressed(Switch(0)) && !switches_.pressed(Switch(1))) &&
          (switches_.released(Switch(0)) || switches_.released(Switch(1)))) {
#endif  // PLAITS_ROVED_PANEL
        pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Unlock();
        pots_[POTS_ADC_CHANNEL_MORPH_POT].Unlock();
        pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Unlock();
        pots_[POTS_ADC_CHANNEL_FREQUENCY_POT].Unlock();
        pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Unlock();
        mode_ = UI_MODE_CHANGE_OPTIONS;
      }
      break;

    case UI_MODE_CHANGE_OPTIONS:
      {
#if PLAITS_ROVED_PANEL
        // TIMBRE + FREQUENCY leaves the menu, matching the gesture that opens
        // it. MORPH/HARMONICS step the value of the selected option.
        if (switches_.pressed(Switch(3)) && switches_.pressed(Switch(0))) {
          ignore_release_[0] = ignore_release_[3] = true;
          SaveState();
          mode_ = UI_MODE_NORMAL;
          break;
        }

        int index_delta = 0;
        if (switches_.released(Switch(3))) {
          index_delta = -1;
        } else if (switches_.released(Switch(0))) {
          index_delta = 1;
        }

        int value_delta = 0;
        if (switches_.released(Switch(1))) {
          value_delta = -1;
        } else if (switches_.released(Switch(2))) {
          value_delta = 1;
        }
#else  // PLAITS_ROVED_PANEL
        if (switches_.pressed(Switch(0)) && switches_.pressed(Switch(1))) {
          ignore_release_[0] = ignore_release_[1] = true;
          SaveState();
          mode_ = UI_MODE_NORMAL;
          break;
        }

        // Two buttons can only step forward, so both deltas wrap upward.
        const int index_delta = switches_.released(Switch(0)) ? 1 : 0;
        const int value_delta = switches_.released(Switch(1)) ? 1 : 0;
#endif  // PLAITS_ROVED_PANEL

        if (index_delta) {
          StepOptionIndex(index_delta);
        }

        if (value_delta) {
          uint8_t* value = NULL;
          int num_values = 0;
          switch (option_index_) {
            case OPTION_LIGHT_CHORD_SET:
              value = &patch_->chord_set_option;
              num_values = kNumChordSetOptions;
              break;
            case OPTION_LIGHT_AUX_OUTPUT:
              value = &patch_->aux_output_option;
              num_values = kNumAuxOutputOptions;
              break;
            case OPTION_LIGHT_SUBOSC:
              value = &patch_->aux_subosc_option;
              num_values = kNumSuboscOptions;
              break;
            case OPTION_LIGHT_FREQUENCY_POT:
              value = &patch_->locked_frequency_pot_option;
              num_values = kNumLockedFrequencyPotOptions;
              break;
            case OPTION_LIGHT_MODEL_CV:
              value = &patch_->model_cv_option;
              num_values = kNumModelCVOptions;
              break;
            case OPTION_LIGHT_LEVEL_CV:
              value = &patch_->level_cv_option;
              num_values = kNumLevelCVOptions;
              break;
            case OPTION_LIGHT_HOLD_ON_TRIGGER:
              value = &patch_->hold_on_trigger_option;
              num_values = kNumHoldOnTriggerOptions;
              break;
            case OPTION_LIGHT_ATTENUVERTER_MODE:
              value = &patch_->attenuverter_mode;
              num_values = kNumAttenuverterModes;
              break;
          }
          if (value) {
            const int old_value = *value;
            const int new_value = CycleOption(
                old_value, num_values, value_delta);
            if (option_index_ == OPTION_LIGHT_FREQUENCY_POT) {
              // Leaving octave switching while the knob is in the locked
              // position freezes its current octave. Returning to it restores
              // the neutral octave. This is keyed on the transition so it
              // behaves the same in either menu direction.
              if (old_value == 0 && new_value != 0 &&
                  PitchRangeFromControl(octave_) == PITCH_RANGE_OCTAVES) {
                locked_octave_ = static_cast<uint8_t>(
                    octave_quantizer_.Process(
                        octave_catch_up_.Process(
                            0.5f * transposition_ + 0.5f)));
              } else if (old_value != 0 && new_value == 0) {
                locked_octave_ = 4;
                octave_catch_up_.Init(
                    0.5f, 0.5f * transposition_ + 0.5f);
              }
            }
            *value = static_cast<uint8_t>(new_value);
          }
        }
      }
      break;

    case UI_MODE_TEST:
    case UI_MODE_ERROR:
      for (int i = 0; i < SWITCH_LAST; ++i) {
        if (switches_.just_pressed(Switch(i))) {
          press_time_[i] = 0;
          ignore_release_[i] = true;
          mode_ = UI_MODE_NORMAL;
        }
      }
      break;
  }
}

void Ui::ProcessPotsHiddenParameters() {
  for (int i = 0; i < POTS_ADC_CHANNEL_LAST; ++i) {
    pots_[i].ProcessUIRate();
  }
  if (locked_octave_gesture_armed_ &&
      pots_[POTS_ADC_CHANNEL_MORPH_POT].editing_hidden_parameter()) {
    editing_locked_octave_ = true;
    locked_octave_ = static_cast<uint8_t>(
        octave_quantizer_.Process(locked_octave_control_));
  }
}

/* static */
const CvAdcChannel Ui::normalized_channels_[] = {
  CV_ADC_CHANNEL_FM,
  CV_ADC_CHANNEL_TIMBRE,
  CV_ADC_CHANNEL_MORPH,
  CV_ADC_CHANNEL_TRIGGER,
  CV_ADC_CHANNEL_LEVEL,
};

void Ui::DetectNormalization() {
  bool expected_value = normalization_probe_state_ >> 31;
  for (int i = 0; i < kNumNormalizedChannels; ++i) {
    CvAdcChannel channel = normalized_channels_[i];
    bool read_value = cv_adc_.value(channel) < \
        settings_->calibration_data(channel).normalization_detection_threshold;
    if (expected_value != read_value) {
      ++normalization_detection_mismatches_[i];
    }
  }

  ++normalization_detection_count_;
  if (normalization_detection_count_ == kProbeSequenceDuration) {
    normalization_detection_count_ = 0;
    bool* destination = &modulations_->frequency_patched;
    for (int i = 0; i < kNumNormalizedChannels; ++i) {
      destination[i] = normalization_detection_mismatches_[i] >= 2;
      normalization_detection_mismatches_[i] = 0;
    }
  }

  normalization_probe_state_ = 1103515245 * normalization_probe_state_ + 12345;
  normalization_probe_.Write(normalization_probe_state_ >> 31);
}

void Ui::Poll() {
  for (int i = 0; i < POTS_ADC_CHANNEL_LAST; ++i) {
    pots_[i].ProcessControlRate(pots_adc_.float_value(PotsAdcChannel(i)));
  }

  float* destination = &modulations_->engine;
  for (int i = 0; i < CV_ADC_CHANNEL_LAST; ++i) {
    destination[i] = settings_->calibration_data(i).Transform(
        cv_adc_.float_value(CvAdcChannel(i)));
  }

  ONE_POLE(pitch_lp_, modulations_->note, 0.7f);
  modulations_->note = pitch_lp_;

#if PLAITS_BUILD_ENABLE_CALIBRATION
  // A much slower filter than the playing one, on the RAW V/OCT reading (the
  // calibration being measured is what turns that into a note), so that each
  // step samples a settled voltage rather than whatever the pitch tracker is
  // chasing.
  ONE_POLE(
      pitch_lp_calibration_, cv_adc_.float_value(CV_ADC_CHANNEL_V_OCT), 0.1f);
#endif  // PLAITS_BUILD_ENABLE_CALIBRATION

  ui_task_ = (ui_task_ + 1) % 4;
  switch (ui_task_) {
    case 0:
      UpdateLEDs();
      break;

    case 1:
      ReadSwitches();
      break;

    case 2:
      ProcessPotsHiddenParameters();
      break;

    case 3:
      DetectNormalization();
      break;
  }

  cv_adc_.Convert();
  pots_adc_.Convert();

  const int pitch_range = PitchRangeFromControl(octave_);

  // Range transitions capture the MANUAL pitch only; V/OCT and FM live in the
  // modulation structure and are never baked into the tuned root. Each new
  // FREQUENCY role starts from its neutral value, then catches the physical
  // knob with the same endpoint-weighted response as Plaits' hidden params.
  if (pitch_range != previous_pitch_range_) {
    if (pitch_range == PITCH_RANGE_PRECISION) {
      precision_anchor_note_ = patch_->note;
      precision_catch_up_.Init(
          0.5f, 0.5f * transposition_ + 0.5f);
    } else if (pitch_range == PITCH_RANGE_OCTAVES) {
      tuned_root_note_ = patch_->note;
      precision_anchor_note_ = tuned_root_note_;
      locked_octave_ = 4;
      octave_catch_up_.Init(
          0.5f, 0.5f * transposition_ + 0.5f);
      SaveState();
    } else if (previous_pitch_range_ == PITCH_RANGE_PRECISION) {
      // Fine Tune is a complete tuning mode, not a temporary editor that must
      // be followed by Octaves. Preserve its final manual pitch when leaving
      // in either direction.
      tuned_root_note_ = patch_->note;
      precision_anchor_note_ = tuned_root_note_;
      SaveState();
    }
    previous_pitch_range_ = pitch_range;
  }

  if (pitch_range == PITCH_RANGE_LOW) {
    patch_->note = -48.37f + transposition_ * 60.0f;
  } else if (pitch_range == PITCH_RANGE_PRECISION) {
    const float fine_control = precision_catch_up_.Process(
        0.5f * transposition_ + 0.5f);
    patch_->note = PrecisionRangeNote(
        precision_anchor_note_, 2.0f * fine_control - 1.0f);
    tuned_root_note_ = patch_->note;
    if (precision_root_save_.Process(EncodeTunedRoot(tuned_root_note_))) {
      SaveState();
    }
  } else if (pitch_range == PITCH_RANGE_OCTAVES) {
    patch_->note = tuned_root_note_;
    if (patch_->locked_frequency_pot_option == 0) {
      const float octave_control = octave_catch_up_.Process(
          0.5f * transposition_ + 0.5f);
      patch_->note += 12.0f * static_cast<float>(
          octave_quantizer_.Process(octave_control) - 4);
      patch_->freqlock_param = 0.0f;
    } else {
      patch_->note += 12.0f * static_cast<float>(locked_octave_ - 4);
      patch_->freqlock_param = 0.5f * transposition_ + 0.5f;
    }
  } else if (pitch_range == PITCH_RANGE_HIGH) {
    patch_->note = 60.0f + transposition_ * 48.0f;
  } else {
    patch_->note = WideRangeNote(pitch_range, transposition_);
  }
}

#if PLAITS_BUILD_ENABLE_CALIBRATION

// The stock two-point CV calibration, restored verbatim in its arithmetic so
// that a module calibrated here matches one calibrated under any other Plaits
// firmware — the numbers land in the same PersistentData chunk (settings.h),
// which no firmware install erases.
void Ui::StartCalibration() {
  calibration_step_ = 1;
  calibration_armed_ = false;
  // The probe injects a test signal into the normalized inputs; the offsets
  // measured below have to see the patched voltages alone.
  normalization_probe_.Disable();
}

void Ui::CalibrateC1() {
  // Acquire offsets for all channels.
  for (int i = 0; i < CV_ADC_CHANNEL_LAST; ++i) {
    if (i != CV_ADC_CHANNEL_V_OCT) {
      ChannelCalibrationData* c = settings_->mutable_calibration_data(i);
      c->offset = -cv_adc_.float_value(CvAdcChannel(i)) * c->scale;
    }
  }
  cv_c1_ = pitch_lp_calibration_;
  calibration_step_ = 2;
}

void Ui::CalibrateC3() {
  // (-33/100.0*1 + -33/140.0 * -10.0) / 3.3 * 2.0 - 1 = 0.228
  float c1 = cv_c1_;

  // (-33/100.0*1 + -33/140.0 * -10.0) / 3.3 * 2.0 - 1 = -0.171
  float c3 = pitch_lp_calibration_;
  float delta = c3 - c1;

  // Two octaves apart, within tolerance. Anything else is a mis-patched or
  // wrongly-scaled source, and is rejected rather than written: on a bad pair
  // the module shows the error display and the previous calibration — the one
  // it has been playing in tune with — is left in flash untouched.
  if (delta > -0.6f && delta < -0.2f) {
    ChannelCalibrationData* c = settings_->mutable_calibration_data(
        CV_ADC_CHANNEL_V_OCT);
    c->scale = 24.0f / delta;
    c->offset = 12.0f - c->scale * c1;
    settings_->SavePersistentData();
    mode_ = UI_MODE_NORMAL;
  } else {
    mode_ = UI_MODE_ERROR;
  }
  calibration_step_ = 0;
  normalization_probe_.Init();
}

#endif  // PLAITS_BUILD_ENABLE_CALIBRATION

}  // namespace plaits
