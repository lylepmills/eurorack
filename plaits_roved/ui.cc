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

static const int32_t kMediumPressTime = 200;
static const int32_t kLongPressTime = 2000;

static const uint8_t kNumOptions = 6;
static const uint8_t kNumLockedFrequencyPotOptions = 3;
static const uint8_t kNumModelCVOptions = 3;
static const uint8_t kNumLevelCVOptions = 2;
static const uint8_t kNumSuboscWaveOptions = 3;
static const uint8_t kNumSuboscOctaveOptions = 3;
static const uint8_t kNumChordSetOptions = 3;

#define ENABLE_LFO_MODE

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

  // Bind pots to parameters.
  pots_[POTS_ADC_CHANNEL_FREQ_POT].Init(
      &transposition_, &patch->freqlock_param, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Init(
      &patch->harmonics, &octave_, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Init(
      &patch->timbre, &patch->lpg_colour, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_MORPH_POT].Init(
      &patch->morph, &patch->decay, 1.0f, 0.0f);
  pots_[POTS_ADC_CHANNEL_TIMBRE_ATTENUVERTER].Init(
      &patch->timbre_modulation_amount, NULL, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Init(
      &patch->frequency_modulation_amount, NULL, 2.0f, -1.0f);
  pots_[POTS_ADC_CHANNEL_MORPH_ATTENUVERTER].Init(
      &patch->morph_modulation_amount, NULL, 2.0f, -1.0f);
  
  LoadState();

  if (switches_.pressed_immediate(SWITCH_ROW_2)) {
    State* state = settings_->mutable_state();
    if (state->color_blind == 1) {
      state->color_blind = 0;
    } else {
      state->color_blind = 1;
    }
    settings_->SaveState();
  }

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
  cv_c1_ = 0.0f;
  pitch_lp_ = 0.0f;
  pitch_lp_calibration_ = 0.0f;
}

void Ui::LoadState() {
  const State& state = settings_->state();
  patch_->engine = state.engine;
  patch_->lpg_colour = static_cast<float>(state.lpg_colour) / 256.0f;
  patch_->decay = static_cast<float>(state.decay) / 256.0f;
  octave_ = static_cast<float>(state.octave) / 256.0f;
  patch_->freqlock_param = static_cast<float>(state.freqlock_param) / 256.0f;
  if (state.frequency_pot_main_parameter < 999.0f) {
    pots_[POTS_ADC_CHANNEL_FREQ_POT].LockMainParameter(state.frequency_pot_main_parameter);
  }

  patch_->locked_frequency_pot_option = state.locked_frequency_pot_option;
  patch_->model_cv_option = state.model_cv_option;
  patch_->level_cv_option = state.level_cv_option;
  patch_->aux_subosc_wave_option = state.aux_output_option & 0x0f;
  patch_->aux_subosc_octave_option = state.aux_output_option >> 4;
  patch_->chord_set_option = state.chord_set_option;
}

void Ui::SaveState() {
  State* state = settings_->mutable_state();
  state->engine = patch_->engine;
  state->lpg_colour = static_cast<uint8_t>(patch_->lpg_colour * 256.0f);
  state->decay = static_cast<uint8_t>(patch_->decay * 256.0f);
  state->octave = static_cast<uint8_t>(octave_ * 256.0f);
  state->freqlock_param = static_cast<uint8_t>(patch_->freqlock_param * 256.0f);
  if (pots_[POTS_ADC_CHANNEL_FREQ_POT].locked()) {
    state->frequency_pot_main_parameter = pots_[POTS_ADC_CHANNEL_FREQ_POT].main_parameter();
  } else {
    state->frequency_pot_main_parameter = 1000.0f;
  }

  state->locked_frequency_pot_option = patch_->locked_frequency_pot_option;
  state->model_cv_option = patch_->model_cv_option;
  state->level_cv_option = patch_->level_cv_option;
  state->aux_output_option = (patch_->aux_subosc_octave_option << 4) + patch_->aux_subosc_wave_option;
  state->chord_set_option = patch_->chord_set_option;

  settings_->SaveState();
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
        LedColor red = settings_->state().color_blind == 1
            ? ((pwm_counter & 7) ? LED_COLOR_OFF : LED_COLOR_YELLOW)
            : LED_COLOR_RED;
        LedColor green = settings_->state().color_blind == 1
            ? LED_COLOR_YELLOW
            : LED_COLOR_GREEN;
        leds_.set(
            active_engine_ & 7,
            active_engine_ & 8 ? red : green);
        if (pwm_counter < triangle) {
          leds_.mask(
              patch_->engine & 7,
              patch_->engine & 8 ? red : green);
        }
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
                parameter * 4 + i,
                value * 64.0f > pwm_counter ? LED_COLOR_YELLOW : LED_COLOR_OFF);
            value -= 0.25f;
          }
        }
      }
      break;
    
    case UI_MODE_DISPLAY_OCTAVE:
      {
#ifdef ENABLE_LFO_MODE
        int octave = static_cast<float>(octave_ * 10.0f);
        for (int i = 0; i < 8; ++i) {
          LedColor color = LED_COLOR_OFF;
          if (octave == 0) {
            color = i == (triangle >> 1) ? LED_COLOR_OFF : LED_COLOR_YELLOW;
          } else if (octave == 9) {
            color = LED_COLOR_YELLOW;
          } else {
            color = (octave - 1) == i ? LED_COLOR_YELLOW : LED_COLOR_OFF;
          }
          leds_.set(i, color);
        }
#else
        int octave = static_cast<float>(octave_ * 9.0f);
        for (int i = 0; i < 8; ++i) {
          leds_.set(
              i,
              octave == i || (octave == 8) ? LED_COLOR_YELLOW : LED_COLOR_OFF);
        }
#endif  // ENABLE_LFO_MODE
      }
      break;
      
    case UI_MODE_CALIBRATION_C1:
      if (pwm_counter < triangle) {
        leds_.set(0, LED_COLOR_GREEN);
      }
      break;

    case UI_MODE_CALIBRATION_C3:
      if (pwm_counter < triangle) {
        leds_.set(0, LED_COLOR_YELLOW);
      }
      break;

    case UI_MODE_FREQUENCY_LOCK:
      for (int i = 0; i < kNumLEDs; ++i) {
        if (pots_[POTS_ADC_CHANNEL_FREQ_POT].locked()) {
          if (settings_->state().color_blind == 0) {
            leds_.set(i, LED_COLOR_RED);
          } else {
            // blink lights in color blind mode
            if (pwm_counter < triangle) {
              leds_.set(i, LED_COLOR_YELLOW);
            }
          }
        } else {
          if (settings_->state().color_blind == 0) {
            leds_.set(i, LED_COLOR_GREEN);
          } else {
            leds_.set(i, LED_COLOR_YELLOW);
          }
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
    int color = (pwm_counter_ >> 10) % 3;
      for (int i = 0; i < kNumLEDs; ++i) {
        leds_.set(
            i, pwm_counter > ((triangle + (i * 2)) & 15)
                ? (color == 0
                   ? LED_COLOR_GREEN
                   : (color == 1 ? LED_COLOR_YELLOW : LED_COLOR_RED))
                : LED_COLOR_OFF);
      }
      break;
  }
  leds_.Write();
}

void Ui::ReadSwitches() {
  switches_.Debounce();
  
  switch (mode_) {
    case UI_MODE_NORMAL:
      {
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
          pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Lock();
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

        // Long, double press: enter calibration mode.
        if (press_time_[2] >= kLongPressTime &&
            press_time_[3] >= kLongPressTime) {
          press_time_[2] = press_time_[3] = 0;
          RealignPots();
          StartCalibration();
        }

        // Long, double press: enter options menu.
        if (press_time_[0] >= kMediumPressTime &&
          press_time_[3] >= kMediumPressTime) {
          press_time_[0] = press_time_[3] = 0;
          mode_ = UI_MODE_CHANGE_OPTIONS_PRE_RELEASE;
        }
        
        // Long, double press: lock coarse frequency.
        if (press_time_[0] >= kMediumPressTime &&
            press_time_[1] >= kMediumPressTime) {
          ignore_release_[0] = true;
          ignore_release_[1] = true;

          RealignPots();
          pots_[POTS_ADC_CHANNEL_FREQ_POT].ToggleLock();
          // The below get locked when we first press each button, unlock them
          // since we are not modifying the other hidden parameters.
          pots_[POTS_ADC_CHANNEL_TIMBRE_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_MORPH_POT].Unlock();
          pots_[POTS_ADC_CHANNEL_HARMONICS_POT].Unlock();

          mode_ = UI_MODE_FREQUENCY_LOCK;
          SaveState();
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

        // 1st switch
        if (switches_.released(Switch(3)) && !ignore_release_[3]) {
          RealignPots();
          if (patch_->engine >= 8) {
            patch_->engine = patch_->engine & 7;
          } else {
            patch_->engine = (patch_->engine + 7) % 8;
          }
          SaveState();
        }
        
        // 2nd switch
        if (switches_.released(Switch(0)) && !ignore_release_[0]) {
          RealignPots();
          if (patch_->engine >= 8) {
            patch_->engine = patch_->engine & 7;
          } else {
            patch_->engine = (patch_->engine + 1) % 8;
          }
          SaveState();
        }
  
        // 3rd switch
        if (switches_.released(Switch(1)) && !ignore_release_[1]) {
          RealignPots();
          if (patch_->engine < 8) {
            patch_->engine = (patch_->engine & 7) + 8;
          } else {
            patch_->engine = 8 + ((patch_->engine + 7) % 8);
          }
          SaveState();
        }

        // 4th switch
        if (switches_.released(Switch(2)) && !ignore_release_[2]) {
          RealignPots();
          if (patch_->engine < 8) {
            patch_->engine = (patch_->engine & 7) + 8;
          } else {
            patch_->engine = 8 + ((patch_->engine + 1) % 8);
          }
          SaveState();
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
          press_time_[i] = 0;
          mode_ = UI_MODE_NORMAL;
        }
      }
      break;
    
    case UI_MODE_CALIBRATION_C1:
      for (int i = 0; i < SWITCH_LAST; ++i) {
        if (switches_.just_pressed(Switch(i))) {
          press_time_[i] = 0;
          ignore_release_[i] = true;
          CalibrateC1();
          break;
        }
      }
      break;
      
    case UI_MODE_CALIBRATION_C3:
      for (int i = 0; i < SWITCH_LAST; ++i) {
        if (switches_.just_pressed(Switch(i))) {
          press_time_[i] = 0;
          ignore_release_[i] = true;
          CalibrateC3();
          break;
        }
      }
      break;

    case UI_MODE_FREQUENCY_LOCK:
      for (int i = 0; i < 2; ++i) {
        if (switches_.released(Switch(i))) {
          press_time_[0] = press_time_[1] = 0;
          mode_ = UI_MODE_NORMAL;
          break;
        }
      }

      break;

    case UI_MODE_CHANGE_OPTIONS_PRE_RELEASE:
      if ((!switches_.pressed(Switch(0)) && !switches_.pressed(Switch(3))) &&
          (switches_.released(Switch(0)) || switches_.released(Switch(3)))) {
        mode_ = UI_MODE_CHANGE_OPTIONS;
      }
      break;

    case UI_MODE_CHANGE_OPTIONS:
      if (switches_.pressed(Switch(0)) && switches_.pressed(Switch(3))) {
        // Add a bit of extra time so holding both buttons doesn't instantly jump 
        // back into the options menu.
        ignore_release_[0] = ignore_release_[3] = true;
        press_time_[0] = press_time_[3] = -2000;
        SaveState();
        mode_ = UI_MODE_NORMAL;
        break;
      }

      if (switches_.released(Switch(3))) {
        option_index_ = (option_index_ - 1 + kNumOptions) % kNumOptions;
      }
      
      if (switches_.released(Switch(0))) {
        option_index_ = (option_index_ + 1) % kNumOptions;
      }

      if (switches_.released(Switch(1)) || switches_.released(Switch(2))) {
        int delta = switches_.released(Switch(1)) ? -1 : 1;

        if (option_index_ == 0) {
          patch_->locked_frequency_pot_option = 
            (patch_->locked_frequency_pot_option + delta + kNumLockedFrequencyPotOptions)
            % kNumLockedFrequencyPotOptions;
        } else if (option_index_ == 1) {
          patch_->model_cv_option = 
            (patch_->model_cv_option + delta + kNumModelCVOptions)
            % kNumModelCVOptions;
        } else if (option_index_ == 2) {
          patch_->level_cv_option = 
            (patch_->level_cv_option + delta + kNumLevelCVOptions)
            % kNumLevelCVOptions;
        } else if (option_index_ == 3) {
          patch_->aux_subosc_wave_option = 
            (patch_->aux_subosc_wave_option + delta + kNumSuboscWaveOptions)
            % kNumSuboscWaveOptions;
        } else if (option_index_ == 4) {
          patch_->aux_subosc_octave_option = 
            (patch_->aux_subosc_octave_option + delta + kNumSuboscOctaveOptions)
            % kNumSuboscOctaveOptions;
        } else if (option_index_ == 5) {
          patch_->chord_set_option = 
            (patch_->chord_set_option + delta + kNumChordSetOptions)
            % kNumChordSetOptions;
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
  ONE_POLE(
      pitch_lp_calibration_, cv_adc_.float_value(CV_ADC_CHANNEL_V_OCT), 0.1f);
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

#ifdef ENABLE_LFO_MODE
  int octave = static_cast<int>(octave_ * 10.0f);
  if (octave == 0) {
    patch_->note = -48.37f + transposition_ * 60.0f;
  } else if (octave == 9) {
    patch_->note = 60.0f + transposition_ * 48.0f;
  } else {
    const float fine = transposition_ * 7.0f;
    patch_->note = fine + static_cast<float>(octave) * 12.0f;
  }
#else
  int octave = static_cast<int>(octave_ * 9.0f);
  if (octave < 8) {
    const float fine = transposition_ * 7.0f;
    patch_->note = fine + static_cast<float>(octave) * 12.0f + 12.0f;
  } else {
    patch_->note = 60.0f + transposition_ * 48.0f;
  }
#endif  // ENABLE_LFO_MODE
}

void Ui::StartCalibration() {
  mode_ = UI_MODE_CALIBRATION_C1;
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
  mode_ = UI_MODE_CALIBRATION_C3;
}

void Ui::CalibrateC3() {
  // (-33/100.0*1 + -33/140.0 * -10.0) / 3.3 * 2.0 - 1 = 0.228
  float c1 = cv_c1_;

  // (-33/100.0*1 + -33/140.0 * -10.0) / 3.3 * 2.0 - 1 = -0.171
  float c3 = pitch_lp_calibration_;
  float delta = c3 - c1;
  
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
  normalization_probe_.Init();
}

// uint8_t Ui::HandleFactoryTestingRequest(uint8_t command) {
//   uint8_t argument = command & 0x1f;
//   command = command >> 5;
//   uint8_t reply = 0;
//   switch (command) {
//     case FACTORY_TESTING_READ_POT:
//       reply = pots_adc_.value(PotsAdcChannel(argument)) >> 8;
//       break;
      
//     case FACTORY_TESTING_READ_CV:
//       reply = (cv_adc_.value(CvAdcChannel(argument)) + 32768) >> 8;
//       break;
    
//     case FACTORY_TESTING_READ_NORMALIZATION:
//       reply = (&modulations_->frequency_patched)[argument] ? 0 : 255;
//       break;      
    
//     case FACTORY_TESTING_READ_GATE:
//       reply = switches_.pressed(Switch(argument));
//       break;
      
//     case FACTORY_TESTING_GENERATE_TEST_SIGNAL:
//       if (argument) {
//         mode_ = UI_MODE_TEST;
//       } else {
//         mode_ = UI_MODE_NORMAL;
//       }
//       break;
      
//     case FACTORY_TESTING_CALIBRATE:
//       {
//         switch (argument) {
//           case 0:
//             patch_->engine = 0;
//             StartCalibration();
//             break;
          
//           case 1:
//             CalibrateC1();
//             break;
          
//           case 2:
//             CalibrateC3();
//             SaveState();
//             break;
//         }
//       }
//       break;
//   }
//   return reply;
// }

}  // namespace plaits
