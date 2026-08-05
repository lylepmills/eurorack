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
// Settings storage.

#include "plaits/settings.h"

#include <algorithm>

#if defined(STM32F37X)
#include <stm32f37x_flash.h>
#endif

#include "stmlib/system/storage.h"
#include "plaits/build_config.h"

namespace plaits {

using namespace std;

#if defined(STM32F37X)
// The audio updater erases and rewrites the application page containing this
// word on every firmware install. Consuming it on first boot lets the firmware
// distinguish a real reflash from an ordinary power cycle, so Starting Options
// are applied for every install without making runtime option changes volatile.
const uint32_t kInstallMarkerValue = 0x5af00d5a;
const uint32_t kInstallMarker
    __attribute__((section(".plaits_install_marker"), aligned(4), used)) =
        kInstallMarkerValue;
#endif

static bool ConsumeInstallMarker() {
#if defined(STM32F37X)
  const volatile uint32_t* marker = &kInstallMarker;
  if (*marker != kInstallMarkerValue) {
    return false;
  }
  FLASH_Unlock();
  FLASH_Status status = FLASH_ProgramWord(
      reinterpret_cast<uint32_t>(&kInstallMarker), 0u);
  FLASH_Lock();
  return status == FLASH_COMPLETE;
#else
  return false;
#endif
}

static void ApplyBuildOptionDefaults(State* state) {
  state->locked_frequency_pot_option = PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION;
  state->model_cv_option = PLAITS_BUILD_MODEL_CV_OPTION;
  state->level_cv_option = PLAITS_BUILD_LEVEL_CV_OPTION;
  state->aux_output_option = PLAITS_BUILD_AUX_OUTPUT_OPTION;
  state->aux_subosc_option = PLAITS_BUILD_AUX_SUBOSC_OPTION;
  state->chord_set_option = PLAITS_BUILD_CHORD_SET_OPTION;
  state->hold_on_trigger_option = PLAITS_BUILD_HOLD_ON_TRIGGER_OPTION;
  state->engine = static_cast<uint8_t>(
      (state->engine & 0x1f) | (PLAITS_BUILD_ATTENUVERTER_MODE << 5));
  state->options_profile_id_low = static_cast<uint8_t>(
      PLAITS_BUILD_OPTIONS_PROFILE_ID & 0xff);
  state->options_profile_id_high = static_cast<uint8_t>(
      (PLAITS_BUILD_OPTIONS_PROFILE_ID >> 8) & 0xff);
  state->options_profile_id_upper = static_cast<uint8_t>(
      (PLAITS_BUILD_OPTIONS_PROFILE_ID >> 16) & 0xff);
}

bool Settings::Init() {
  InitPersistentData();
  InitState();
  
  bool success = chunk_storage_.Init(&persistent_data_, &state_);

  bool fresh_install = ConsumeInstallMarker();
  uint32_t saved_options_profile_id = state_.options_profile_id_low |
      (state_.options_profile_id_high << 8) |
      (state_.options_profile_id_upper << 16);
  bool apply_build_options = success &&
      (fresh_install ||
       saved_options_profile_id != PLAITS_BUILD_OPTIONS_PROFILE_ID);
  if (apply_build_options) {
    ApplyBuildOptionDefaults(&state_);
  }

  // The engine byte's low five bits store the selected slot (the firmware has
  // a hard 32-slot ceiling); bits 5-6 persist the three-state attenuverter mode
  // without growing State and invalidating every previously saved setting.
  int saved_engine = state_.engine & 0x1f;
  int attenuverter_mode = (state_.engine >> 5) & 0x03;
  CONSTRAIN(saved_engine, 0, PLAITS_ENGINE_COUNT - 1);
  CONSTRAIN(attenuverter_mode, 0, 2);
  state_.engine = static_cast<uint8_t>(
      saved_engine | (attenuverter_mode << 5));
  CONSTRAIN(
      state_.locked_frequency_pot_option,
      0,
      3 + 2 * PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE);
  CONSTRAIN(state_.model_cv_option, 0, 3);
  CONSTRAIN(state_.level_cv_option, 0, 2);
  CONSTRAIN(state_.aux_output_option, 0, 2);
  CONSTRAIN(state_.aux_subosc_option, 0, 5);
  CONSTRAIN(state_.chord_set_option, 0, PLAITS_CHORD_TABLE_COUNT - 1);
  CONSTRAIN(state_.hold_on_trigger_option, 0, 1);
  CONSTRAIN(state_.locked_octave, 0, 8);

  if (apply_build_options) {
    SaveState();
  }

  return success;
}

void Settings::InitPersistentData() {
  ChannelCalibrationData* c = persistent_data_.channel_calibration_data;
  c[CV_ADC_CHANNEL_MODEL].offset = 0.025f;
  c[CV_ADC_CHANNEL_MODEL].scale = -1.03f;
  c[CV_ADC_CHANNEL_MODEL].normalization_detection_threshold = 0;
  
  c[CV_ADC_CHANNEL_V_OCT].offset = 25.71;
  c[CV_ADC_CHANNEL_V_OCT].scale = -60.0f;
  c[CV_ADC_CHANNEL_V_OCT].normalization_detection_threshold = 0;

  c[CV_ADC_CHANNEL_FM].offset = 0.0f;
  c[CV_ADC_CHANNEL_FM].scale = -60.0f;
  c[CV_ADC_CHANNEL_FM].normalization_detection_threshold = -2945;

  c[CV_ADC_CHANNEL_HARMONICS].offset = 0.0f;
  c[CV_ADC_CHANNEL_HARMONICS].scale = -1.0f;
  c[CV_ADC_CHANNEL_HARMONICS].normalization_detection_threshold = 0;

  c[CV_ADC_CHANNEL_TIMBRE].offset = 0.0f;
  c[CV_ADC_CHANNEL_TIMBRE].scale = -1.6f;
  c[CV_ADC_CHANNEL_TIMBRE].normalization_detection_threshold = -2945;

  c[CV_ADC_CHANNEL_MORPH].offset = 0.0f;
  c[CV_ADC_CHANNEL_MORPH].scale = -1.6f;
  c[CV_ADC_CHANNEL_MORPH].normalization_detection_threshold = -2945;

  c[CV_ADC_CHANNEL_TRIGGER].offset = 0.4f;
  c[CV_ADC_CHANNEL_TRIGGER].scale = -0.6f;
  c[CV_ADC_CHANNEL_TRIGGER].normalization_detection_threshold = 13663;

  c[CV_ADC_CHANNEL_LEVEL].offset = 0.49f;
  c[CV_ADC_CHANNEL_LEVEL].scale = -0.6f;
  c[CV_ADC_CHANNEL_LEVEL].normalization_detection_threshold = 21403;
}

void Settings::InitState() {
  // base firmware
  state_.engine = 8;
  state_.lpg_colour = 0;
  state_.decay = 128;
  state_.octave = 255;
  state_.fine_tune = 128;
  state_.options_profile_id_upper = static_cast<uint8_t>(
      (PLAITS_BUILD_OPTIONS_PROFILE_ID >> 16) & 0xff);

  // alt firmware options
  ApplyBuildOptionDefaults(&state_);

  // alt firmware other
  state_.locked_octave = 4;

  // Per-bank navigation memory starts at the first engine of every bank.
  for (int i = 0; i < 4; ++i) {
    state_.bank_last_row[i] = 0;
  }

  state_.tuned_root_q8 = 60 * 256;
  state_.tuned_root_valid = 0;
}

void Settings::SavePersistentData() {
  chunk_storage_.SavePersistentData();
}

void Settings::SaveState() {
  chunk_storage_.SaveState();
}

}  // namespace plaits
