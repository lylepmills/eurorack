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

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/system/system_clock.h"

namespace plaits {

using namespace std;
using namespace stmlib;

static const int32_t kLongPressTime = 2000;

static const uint8_t kNumOptions = 8;
static const uint8_t kNumLockedFrequencyPotOptions = 3;
static const uint8_t kNumModelCVOptions = 3;
static const uint8_t kNumLevelCVOptions = 2;
static const uint8_t kNumSuboscWaveOptions = 3;
static const uint8_t kNumSuboscOctaveOptions = 3;
static const uint8_t kNumChordSetOptions = 3;
static const uint8_t kNumHoldOnTriggerOptions = 2;
static const uint8_t kNumNavigationOptions = 2;

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
      &transposition_, &fine_tune_, 0.005f, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Init(
      &patch->harmonics, &octave_, 0.005f, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Init(
      &patch->timbre, &patch->lpg_colour, 0.01f, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_MORPH_POT].Init(
      &patch->morph, &patch->decay, 0.01f, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_TIMBRE_ATTENUVERTER].Init(
      &patch->timbre_modulation_amount, NULL, 0.005f, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Init(
      &patch->frequency_modulation_amount, &extra_fine_tune_, 0.005f, 2.0f, -1.0f);
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

  locked_octave_ = 4;
}

void Ui::LoadState() {
  const State& state = settings_->state();
  patch_->engine = state.engine;
  patch_->lpg_colour = static_cast<float>(state.lpg_colour) / 256.0f;
  patch_->decay = static_cast<float>(state.decay) / 256.0f;
  octave_ = static_cast<float>(state.octave) / 256.0f;
  fine_tune_ = static_cast<float>(state.fine_tune) / 256.0f;
  extra_fine_tune_ = static_cast<float>(state.extra_fine_tune) / 256.0f;

  // alt firmware
  patch_->locked_frequency_pot_option = state.locked_frequency_pot_option;
  patch_->model_cv_option = state.model_cv_option;
  patch_->level_cv_option = state.level_cv_option;
  patch_->aux_subosc_wave_option = state.aux_subosc_wave_option;
  patch_->aux_subosc_octave_option = state.aux_subosc_octave_option;
  patch_->chord_set_option = state.chord_set_option;
  patch_->hold_on_trigger_option = state.hold_on_trigger_option;
  enable_alt_navigation_ = state.navigation_option == 1;
  locked_octave_ = state.locked_octave;
}

void Ui::SaveState() {
  State* state = settings_->mutable_state();
  state->engine = patch_->engine;
  state->lpg_colour = static_cast<uint8_t>(patch_->lpg_colour * 256.0f);
  state->decay = static_cast<uint8_t>(patch_->decay * 256.0f);
  state->octave = static_cast<uint8_t>(octave_ * 256.0f);
  state->fine_tune = static_cast<uint8_t>(fine_tune_ * 256.0f);
  state->extra_fine_tune = static_cast<uint8_t>(extra_fine_tune_ * 256.0f);

  // alt firmware
  state->locked_frequency_pot_option = patch_->locked_frequency_pot_option;
  state->model_cv_option = patch_->model_cv_option;
  state->level_cv_option = patch_->level_cv_option;
  state->aux_subosc_wave_option = patch_->aux_subosc_wave_option;
  state->aux_subosc_octave_option = patch_->aux_subosc_octave_option;
  state->chord_set_option = patch_->chord_set_option;
  state->hold_on_trigger_option = patch_->hold_on_trigger_option;
  state->navigation_option = enable_alt_navigation_ ? 1 : 0;
  state->locked_octave = locked_octave_;

  settings_->SaveState();
}

uint32_t Ui::BankToColor(int bank) {
  uint32_t colors[3] = { LED_COLOR_YELLOW, LED_COLOR_GREEN, LED_COLOR_RED };
  return colors[bank];
}

void Ui::UpdateLEDs() {
  leds_.Clear();
  ++pwm_counter_;

  int pwm_counter = pwm_counter_ & 15;
  int triangle = (pwm_counter_ >> 4) & 31;
  triangle = triangle < 16 ? triangle : 31 - triangle;

  switch (mode_) {
    case UI_MODE_NORMAL:
      {
        // Selected with the buttons
        const int selected_row = patch_->engine % 8;
        const int selected_bank = patch_->engine / 8;
        uint32_t selected_color = pwm_counter < triangle
            ? BankToColor(selected_bank)
            : LED_COLOR_OFF;

        // With the CV modulation applied
        const int active_row = active_engine_ % 8;
        const int active_bank = active_engine_ / 8;
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
                parameter * 4 + 3 - i,
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
        int octave = static_cast<float>(octave_ * 11.0f);
        for (int i = 0; i < 8; ++i) {
          LedColor color = LED_COLOR_OFF;
          if (octave == 0) {
            color = i == (triangle >> 1) ? LED_COLOR_OFF : LED_COLOR_YELLOW;
          } else if (octave == 10) {
            color = LED_COLOR_YELLOW;
          } else if (octave == 9) {
            color = (i & 1) == ((triangle >> 3) & 1)
                ? LED_COLOR_OFF
                : LED_COLOR_YELLOW;
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
        int option_value = 0;
        if (i == 0) {
          option_value = patch_->locked_frequency_pot_option;
        } else if (i == 1) {
          option_value = patch_->model_cv_option;
        } else if (i == 2) {
          option_value = patch_->level_cv_option;
        } else if (i == 3) {
          option_value = patch_->aux_subosc_wave_option;
        } else if (i == 4) {
          option_value = patch_->aux_subosc_octave_option;
        } else if (i == 5) {
          option_value = patch_->chord_set_option;
        } else if (i == 6) {
          option_value = patch_->hold_on_trigger_option;
        } else if (i == 7) {
          option_value = enable_alt_navigation_ ? 1 : 0;
        }

        LedColor color = LED_COLOR_OFF;
        if (option_value == 0 || option_value == 3) {
          color = LED_COLOR_GREEN;
        } else if (option_value == 1 || option_value == 4) {
          color = LED_COLOR_RED;
        } else if (option_value == 2 || option_value == 5) {
          color = LED_COLOR_YELLOW;
        }
        if ((option_value > 2) && (pwm_counter_ & 128)) {
          color = LED_COLOR_OFF;
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
  ignore_release_[0] = ignore_release_[1] = true;
  RealignPots();
  uint8_t increment = button == 0 ? 23 : 1;
  if (enable_alt_navigation_) {
    if (button == 1) {
      // change bank
      increment = 8;
    } else {
      // change preset within bank
      increment = patch_->engine % 8 != 7 ? 1 : 17;
    }
  }
  patch_->engine = (patch_->engine + increment) % 24;

  SaveState();
}

void Ui::ReadSwitches() {
  switches_.Debounce();

  switch (mode_) {
    case UI_MODE_NORMAL:
      {
        // Press both buttons to enter options menu
        if ((switches_.just_pressed(Switch(0)) && switches_.pressed(Switch(1))) ||
            (switches_.just_pressed(Switch(1)) && switches_.pressed(Switch(0)))) {
          mode_ = UI_MODE_CHANGE_OPTIONS_PRE_RELEASE;
          break;
        }

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

        if (switches_.just_pressed(Switch(0))) {
          pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Lock();
          pots_[POTS_ADC_CHANNEL_MORPH_POT].Lock();
        }
        if (switches_.just_pressed(Switch(1))) {
          pots_[POTS_ADC_CHANNEL_FREQUENCY_POT].Lock();
          pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Lock();
          pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Lock();
        }

        if (pots_[POTS_ADC_CHANNEL_MORPH_POT].editing_hidden_parameter() ||
            pots_[POTS_ADC_CHANNEL_TIMBRE_POT].editing_hidden_parameter()) {
          mode_ = UI_MODE_DISPLAY_ALTERNATE_PARAMETERS;
        }

        if (pots_[POTS_ADC_CHANNEL_HARMONICS_POT].editing_hidden_parameter() ||
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
          Navigate(1);
        }
      }
      break;

    case UI_MODE_DISPLAY_ALTERNATE_PARAMETERS:
    case UI_MODE_DISPLAY_OCTAVE:
      for (int i = 0; i < SWITCH_LAST; ++i) {
        if (switches_.released(Switch(i))) {
          pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_MORPH_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_FREQUENCY_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Unlock();
          press_time_[i] = 0;
          mode_ = UI_MODE_NORMAL;
        }
      }
      break;

    case UI_MODE_DISPLAY_DATA_TRANSFER_PROGRESS:
      break;

    case UI_MODE_CHANGE_OPTIONS_PRE_RELEASE:
      if ((!switches_.pressed(Switch(0)) && !switches_.pressed(Switch(1))) &&
          (switches_.released(Switch(0)) || switches_.released(Switch(1)))) {
        pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Unlock();
        pots_[POTS_ADC_CHANNEL_MORPH_POT].Unlock();
        pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Unlock();
        pots_[POTS_ADC_CHANNEL_FREQUENCY_POT].Unlock();
        pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Unlock();
        mode_ = UI_MODE_CHANGE_OPTIONS;
      }
      break;

    case UI_MODE_CHANGE_OPTIONS:
      if (switches_.pressed(Switch(0)) && switches_.pressed(Switch(1))) {
        ignore_release_[0] = ignore_release_[1] = true;
        SaveState();
        mode_ = UI_MODE_NORMAL;
        break;
      }

      if (switches_.released(Switch(0))) {
        option_index_ += 1;
        if (option_index_ >= kNumOptions) {
          option_index_ = 0;
        }
      }

      if (switches_.released(Switch(1))) {
        if (option_index_ == 0) {
          if (patch_->locked_frequency_pot_option == 0 && static_cast<int>(octave_ * 11.0f) == 9) {
            locked_octave_ = static_cast<uint8_t>(octave_quantizer_.Process(0.5f * transposition_ + 0.5f));
          } else if (patch_->locked_frequency_pot_option == 1) {
            locked_octave_ = 4;
          }
          patch_->locked_frequency_pot_option += 1;
          if (patch_->locked_frequency_pot_option >= kNumLockedFrequencyPotOptions) {
            patch_->locked_frequency_pot_option = 0;
          }
        } else if (option_index_ == 1) {
          patch_->model_cv_option += 1;
          if (patch_->model_cv_option >= kNumModelCVOptions) {
            patch_->model_cv_option = 0;
          }
        } else if (option_index_ == 2) {
          patch_->level_cv_option += 1;
          if (patch_->level_cv_option >= kNumLevelCVOptions) {
            patch_->level_cv_option = 0;
          }
        } else if (option_index_ == 3) {
          patch_->aux_subosc_wave_option += 1;
          if (patch_->aux_subosc_wave_option >= kNumSuboscWaveOptions) {
            patch_->aux_subosc_wave_option = 0;
          }
        } else if (option_index_ == 4) {
          patch_->aux_subosc_octave_option += 1;
          if (patch_->aux_subosc_octave_option >= kNumSuboscOctaveOptions) {
            patch_->aux_subosc_octave_option = 0;
          }
        } else if (option_index_ == 5) {
          patch_->chord_set_option += 1;
          if (patch_->chord_set_option >= kNumChordSetOptions) {
            patch_->chord_set_option = 0;
          }
        } else if (option_index_ == 6) {
          patch_->hold_on_trigger_option += 1;
          if (patch_->hold_on_trigger_option >= kNumHoldOnTriggerOptions) {
            patch_->hold_on_trigger_option = 0;
          }
        } else if (option_index_ == 7) {
          enable_alt_navigation_ = !enable_alt_navigation_;
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

  const int octave = static_cast<int>(octave_ * 11.0f);
  if (octave == 0) {
    patch_->note = -48.37f + transposition_ * 60.0f;
  } else if (octave == 9) {
    patch_->note = 53.0f + fine_tune_ * 14.0f + extra_fine_tune_;
    if (patch_->locked_frequency_pot_option == 0) {
      patch_->note += 12.0f * static_cast<float>(octave_quantizer_.Process(0.5f * transposition_ + 0.5f) - 4);
      patch_->freqlock_param = 0.0f;
    } else {
      patch_->note += 12.0f * static_cast<float>(locked_octave_ - 4);
      patch_->freqlock_param = 0.5f * transposition_ + 0.5f;
    }
  } else if (octave == 10) {
    patch_->note = 60.0f + transposition_ * 48.0f;
  } else {
    const float fine = transposition_ * 7.0f;
    patch_->note = fine + static_cast<float>(octave) * 12.0f;
  }
}

}  // namespace plaits
