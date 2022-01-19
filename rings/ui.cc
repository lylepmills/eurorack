// Copyright 2015 Emilie Gillet.
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
// User interface.

#include "rings/ui.h"

#include <algorithm>

#include "stmlib/system/system_clock.h"

#include "rings/cv_scaler.h"
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"

namespace rings {

const int32_t kAnimationDuration = 2000;
const int32_t kLongPressDuration = 3000;
const int32_t kMediumPressDuration = 200;
const uint8_t kNumOptions = 3;

using namespace std;
using namespace stmlib;

void Ui::Init(
    Settings* settings,
    CvScaler* cv_scaler,
    Part* part,
    StringSynthPart* string_synth) {
  leds_.Init();
  switches_.Init();
  
  settings_ = settings;
  cv_scaler_ = cv_scaler;
  part_ = part;
  string_synth_ = string_synth;
  
  if (switches_.pressed_immediate(1)) {
    State* state = settings_->mutable_state();
    if (state->color_blind == 1) {
      state->color_blind = 0; 
    } else {
      state->color_blind = 1; 
    }
    settings_->Save();
  }
  
  part_->set_polyphony(settings_->state().polyphony);
  part_->set_model(static_cast<ResonatorModel>(settings_->state().model));
  string_synth_->set_polyphony(settings_->state().polyphony);
  string_synth_->set_fx(static_cast<FxType>(settings_->state().model));
  mode_ = UI_MODE_NORMAL;
}

void Ui::SaveState() {
  settings_->mutable_state()->polyphony = part_->polyphony();
  settings_->mutable_state()->model = part_->model();
  settings_->Save();
}

void Ui::Poll() {
  // 1kHz.
  system_clock.Tick();
  switches_.Debounce();
  
  for (uint8_t i = 0; i < kNumSwitches; ++i) {
    if (switches_.just_pressed(i)) {
      if (mode_ == UI_MODE_OPTIONS_MENU && switches_.pressed(1 - i)) {
        IgnoreSwitchReleases();
        queue_.Touch();
        mode_ = UI_MODE_OPTIONS_MENU_OUTRO;
      } else {
        press_time_[i] = system_clock.milliseconds();
      }
    }
    if (switches_.pressed(i) && press_time_[i] != 0) {
      int32_t pressed_time = system_clock.milliseconds() - press_time_[i];
      if (pressed_time > kLongPressDuration) {
        queue_.AddEvent(CONTROL_SWITCH_HOLD, i, pressed_time);
        press_time_[i] = 0;
      } else if (pressed_time > kMediumPressDuration &&
                 mode_ == UI_MODE_NORMAL &&
                 switches_.pressed(1 - i)) {
        settings_->ToggleFrequencyLocking();
        mode_ = UI_MODE_DISPLAY_FREQUENCY_LOCKING;
      }
    }
    if (switches_.released(i) && press_time_[i] != 0) {
      queue_.AddEvent(
          CONTROL_SWITCH,
          i,
          system_clock.milliseconds() - press_time_[i] + 1);
      press_time_[i] = 0;
    }
  }
  
  bool blink = (system_clock.milliseconds() & 127) > 64;
  bool slow_blink = (system_clock.milliseconds() & 255) > 128;
  switch (mode_) {
    case UI_MODE_NORMAL:
      {
        uint8_t pwm_counter = system_clock.milliseconds() & 15;
        uint8_t triangle = (system_clock.milliseconds() >> 5) & 31;
        triangle = triangle < 16 ? triangle : 31 - triangle;

        if (settings_->state().color_blind == 1) {
          uint8_t mode_red_brightness[] = {
            0, 15, 1,
            0, triangle, uint8_t(triangle >> 3)
          };
          uint8_t mode_green_brightness[] = {
            4, 15, 0, 
            uint8_t(triangle >> 1), triangle, 0,
          };
          
          uint8_t poly_counter = (system_clock.milliseconds() >> 7) % 12;
          uint8_t poly_brightness = (poly_counter >> 1) < part_->polyphony() &&
                (poly_counter & 1);
          uint8_t poly_red_brightness = part_->polyphony() >= 2
              ? 8 + 8 * poly_brightness
              : 0;
          uint8_t poly_green_brightness = part_->polyphony() <= 3
              ? 8 + 8 * poly_brightness
              : 0;
          if (part_->polyphony() == 1 || part_->polyphony() == 4) {
            poly_red_brightness >>= 3;
            poly_green_brightness >>= 2;
          }
          leds_.set(
              0,
              pwm_counter < poly_red_brightness,
              pwm_counter < poly_green_brightness);
          leds_.set(
              1,
              pwm_counter < mode_red_brightness[part_->model()],
              pwm_counter < mode_green_brightness[part_->model()]);
        } else {
          leds_.set(0, part_->polyphony() >= 2, part_->polyphony() <= 2);
          leds_.set(1, part_->model() >= 1, part_->model() <= 1);
          // Fancy modes!
          if (part_->polyphony() == 3) {
            leds_.set(0, true, pwm_counter < triangle);
          }
          if (part_->model() >= 3) {
            bool led_1 = part_->model() >= 4 && pwm_counter < triangle;
            bool led_2 = part_->model() <= 4 && pwm_counter < triangle;
            leds_.set(1, led_1, led_2);
          }
        }
        ++strumming_flag_interval_;
        if (strumming_flag_counter_) {
          --strumming_flag_counter_;
          leds_.set(0, false, false);
        }
      }
      break;
    
    case UI_MODE_CALIBRATION_C1:
      leds_.set(0, blink, blink);
      leds_.set(1, false, false);
      break;

    case UI_MODE_CALIBRATION_C3:
      leds_.set(0, false, false);
      leds_.set(1, blink, blink);
      break;

    case UI_MODE_CALIBRATION_LOW:
      leds_.set(0, slow_blink, 0);
      leds_.set(1, slow_blink, 0);
      break;

    case UI_MODE_CALIBRATION_HIGH:
      leds_.set(0, false, slow_blink);
      leds_.set(1, false, slow_blink);
      break;

    case UI_MODE_DISPLAY_FREQUENCY_LOCKING:
      if (settings_->state().frequency_locked) {
        leds_.set(0, blink, 0);
        leds_.set(1, blink, 0);
      } else {
        leds_.set(0, 0, blink);
        leds_.set(1, 0, blink);
      }
      break;

    case UI_MODE_OPTIONS_MENU:
      {
        uint8_t option_value = 0;
        if (option_menu_item_ == 0) {
          leds_.set(0, 0, 1);
          option_value = settings_->ModeOption();
        } else if (option_menu_item_ == 1) {
          leds_.set(0, 1, 0);
          option_value = settings_->WaveformExciterOption();
        } else if (option_menu_item_ == 2) {
          leds_.set(0, 1, 1);
          option_value = settings_->ChordTableOption();
        }

        // Special casing mode options for color consistency.
        if (option_menu_item_ == 0) {
          if (option_value == 0) {
            leds_.set(1, 0, 1);
          } else if (option_value == 1) {
            leds_.set(1, 0, slow_blink);
          } else if (option_value == 2) {
            leds_.set(1, 1, 0);
          } else if (option_value == 3) {
            leds_.set(1, slow_blink, 0);
          } else if (option_value == 4) {
            leds_.set(1, 1, 1);
          }
        } else {
          if (option_value == 0) {
            leds_.set(1, 0, 1);
          } else if (option_value == 1) {
            leds_.set(1, 1, 0);
          } else if (option_value == 2) {
            leds_.set(1, 1, 1);
          } else if (option_value == 3) {
            leds_.set(1, 0, slow_blink);
          } else if (option_value == 4) {
            leds_.set(1, slow_blink, 0);
          } else if (option_value == 5) {
            leds_.set(1, slow_blink, slow_blink);
          } else if (option_value == 6) {
            leds_.set(1, 0, blink);
          } else if (option_value == 7) {
            leds_.set(1, blink, 0);
          } else if (option_value == 8) {
            leds_.set(1, blink, blink);
          }
        }
      }
      break;
    
    case UI_MODE_OPTIONS_MENU_INTRO:
      {
        uint8_t pwm_counter = system_clock.milliseconds() & 15;
        uint8_t triangle_1 = (system_clock.milliseconds() / 7) & 31;
        uint8_t triangle_2 = (system_clock.milliseconds() / 17) & 31;
        triangle_1 = triangle_1 < 16 ? triangle_1 : 31 - triangle_1;
        triangle_2 = triangle_2 < 16 ? triangle_2 : 31 - triangle_2;
        leds_.set(
            0,
            triangle_1 > pwm_counter,
            triangle_2 > pwm_counter);
        leds_.set(
            1,
            triangle_2 > pwm_counter,
            triangle_1 > pwm_counter);
      }
      break;

    case UI_MODE_OPTIONS_MENU_OUTRO:
      {
        uint8_t pwm_counter = 7;
        uint8_t triangle_1 = (system_clock.milliseconds() / 9) & 31;
        uint8_t triangle_2 = (system_clock.milliseconds() / 13) & 31;
        triangle_1 = triangle_1 < 16 ? triangle_1 : 31 - triangle_1;
        triangle_2 = triangle_2 < 16 ? triangle_2 : 31 - triangle_2;
        leds_.set(0, triangle_1 < pwm_counter, triangle_1 > pwm_counter);
        leds_.set(1, triangle_2 > pwm_counter, triangle_2 < pwm_counter);
      }
      break;
    
    case UI_MODE_PANIC:
      leds_.set(0, blink, false);
      leds_.set(1, blink, false);
      break;
  }
  leds_.Write();
}

void Ui::FlushEvents() {
  queue_.Flush();
}

void Ui::IgnoreSwitchReleases() {
  press_time_[0] = press_time_[1] = 0;
}

void Ui::OnSwitchLongHeld(const Event& e) {
  // If both switches are held with a long press, either enter/exit menu
  // or go to calibration.
  if (switches_.pressed(1 - e.control_id)) {
    // Toggle back frequency locking after it was affected by initiating long-press
    settings_->ToggleFrequencyLocking();
    if (e.control_id == 0) {
      mode_ = UI_MODE_OPTIONS_MENU_INTRO;
    } else {
      if (mode_ == UI_MODE_CALIBRATION_C1) {
        StartNormalizationCalibration();
      } else {
        StartCalibration();
      }
    }
  } else if (e.control_id == 0) {
    part_->set_polyphony(3);
    string_synth_->set_polyphony(3);
    SaveState();
  } else {
    int32_t model = part_->model();
    if (model >= 3) {
      model -= 3;
    } else {
      model += 3;
    }
    part_->set_model(static_cast<ResonatorModel>(model));
    string_synth_->set_fx(static_cast<FxType>(model));
  }
  IgnoreSwitchReleases();
}

void Ui::OnSwitchReleased(const Event& e) {
  if (e.control_id == 0) {
    switch (mode_) {
      case UI_MODE_CALIBRATION_C1:
        CalibrateC1();
        break;
      case UI_MODE_CALIBRATION_C3:
        CalibrateC3();
        break;
      case UI_MODE_CALIBRATION_LOW:
        CalibrateLow();
        break;
      case UI_MODE_CALIBRATION_HIGH:
        CalibrateHigh();
        break;
      case UI_MODE_OPTIONS_MENU_INTRO:
      case UI_MODE_OPTIONS_MENU_OUTRO:
        break;
      case UI_MODE_OPTIONS_MENU:
        option_menu_item_ = (option_menu_item_ + 1) % kNumOptions;
        break;
      case UI_MODE_DISPLAY_FREQUENCY_LOCKING:
        IgnoreSwitchReleases();
        mode_ = UI_MODE_NORMAL;
        break;
      default:
        {
          int32_t polyphony = part_->polyphony();
          if (polyphony == 3) {
            polyphony = 2;
          }
          polyphony <<= 1;
          if (polyphony > 4) {
            polyphony = 1;
          }
          part_->set_polyphony(polyphony);
          string_synth_->set_polyphony(polyphony);
          SaveState();
        }
        break;
      }
  } else {
    switch (mode_) {
      case UI_MODE_DISPLAY_FREQUENCY_LOCKING:
        IgnoreSwitchReleases();
        mode_ = UI_MODE_NORMAL;
        break;
      case UI_MODE_OPTIONS_MENU_INTRO:
      case UI_MODE_OPTIONS_MENU_OUTRO:
        break;
      case UI_MODE_OPTIONS_MENU:
        if (option_menu_item_ == 0) {
          settings_->SwitchModeOption();
        } else if (option_menu_item_ == 1) {
          settings_->SwitchWaveformExciterOption();
        } else if (option_menu_item_ == 2) {
          settings_->SwitchChordTableOption();
        }
        break;
      default:
        int32_t model = part_->model();
        if (model >= 3) {
          model -= 3;
        } else {
          model = (model + 1) % 3;
        }
        part_->set_model(static_cast<ResonatorModel>(model));
        string_synth_->set_fx(static_cast<FxType>(model));
        SaveState();
        break;
    }
  }
}

void Ui::StartCalibration() {
  mode_ = UI_MODE_CALIBRATION_C1;
}

void Ui::CalibrateC1() {
  cv_scaler_->CalibrateC1();
  cv_scaler_->CalibrateOffsets();
  mode_ = UI_MODE_CALIBRATION_C3;
}

void Ui::CalibrateC3() {
  bool success = cv_scaler_->CalibrateC3();
  if (success) {
    settings_->Save();
    mode_ = UI_MODE_NORMAL;
  } else {
    mode_ = UI_MODE_PANIC;
  }
}

void Ui::StartNormalizationCalibration() {
  cv_scaler_->StartNormalizationCalibration();
  mode_ = UI_MODE_CALIBRATION_LOW;
}

void Ui::CalibrateLow() {
  cv_scaler_->CalibrateLow();
  mode_ = UI_MODE_CALIBRATION_HIGH;
}

void Ui::CalibrateHigh() {
  bool success = cv_scaler_->CalibrateHigh();
  if (success) {
    settings_->Save();
    mode_ = UI_MODE_NORMAL;
  } else {
    mode_ = UI_MODE_PANIC;
  }
}

void Ui::DoEvents() {
  while (queue_.available()) {
    Event e = queue_.PullEvent();
    if (e.control_type == CONTROL_SWITCH) {
      OnSwitchReleased(e);
    } else if (e.control_type == CONTROL_SWITCH_HOLD) {
      OnSwitchLongHeld(e);
    }
  }
  if (queue_.idle_time() > 800 && mode_ == UI_MODE_PANIC) {
    mode_ = UI_MODE_NORMAL;
  }
  if (mode_ == UI_MODE_OPTIONS_MENU_INTRO) {
    if (queue_.idle_time() > kAnimationDuration) {
      mode_ = UI_MODE_OPTIONS_MENU;
      queue_.Touch();
    }
  } else if (mode_ == UI_MODE_OPTIONS_MENU_OUTRO) {
    if (queue_.idle_time() > kAnimationDuration) {
      mode_ = UI_MODE_NORMAL;
      queue_.Touch();
    }
  } else if (queue_.idle_time() > 1000) {
    queue_.Touch();
  }
}

uint8_t Ui::HandleFactoryTestingRequest(uint8_t command) {
  uint8_t argument = command & 0x1f;
  command = command >> 5;
  uint8_t reply = 0;
  switch (command) {
    case FACTORY_TESTING_READ_POT:
    case FACTORY_TESTING_READ_CV:
      reply = cv_scaler_->adc_value(argument);
      break;
    
    case FACTORY_TESTING_READ_NORMALIZATION:
      reply = cv_scaler_->normalization(argument);
      break;      
    
    case FACTORY_TESTING_READ_GATE:
      reply = argument == 2
          ? cv_scaler_->gate_value()
          : switches_.pressed(argument);
      break;
      
    case FACTORY_TESTING_SET_BYPASS:
      part_->set_bypass(argument);
      break;
      
    case FACTORY_TESTING_CALIBRATE:
      {
        switch (argument) {
          case 0:
            StartCalibration();
            break;
          
          case 1:
            CalibrateC1();
            break;
          
          case 2:
            CalibrateC3();
            break;
          
          case 3:
            StartNormalizationCalibration();
            break;

          case 4:
            CalibrateLow();
            break;
          
          case 5:
            CalibrateHigh();
            queue_.Touch();
            break;
        }
      }
      break;
  }
  return reply;
}

}  // namespace rings
