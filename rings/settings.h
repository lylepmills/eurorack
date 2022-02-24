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
// Settings storage.

#ifndef RINGS_SETTINGS_H_
#define RINGS_SETTINGS_H_

#include "stmlib/stmlib.h"

#include "rings/drivers/adc.h"

namespace rings {

const uint32_t kSettingsId = 414344649;

struct CalibrationData {
  float pitch_offset;
  float pitch_scale;
  float offset[ADC_CHANNEL_NUM_OFFSETS];
  float normalization_detection_threshold;
  uint8_t padding[4];
};

struct State {
  uint32_t settings_version_id;
  float locked_transpose;
  uint8_t polyphony;
  uint8_t model;
  uint8_t easter_egg;
  uint8_t color_blind;
  uint8_t frequency_locked;
  uint8_t mode_option;
  uint8_t waveform_exciter_option;
  uint8_t chord_table_option;
  uint8_t strum_hold_option;
};

struct SettingsData {
  CalibrationData calibration_data; // 40 bytes
  State state;  // 13 bytes
  uint8_t padding[11];
};

class Settings {
 public:
  Settings() { }
  ~Settings() { }
  
  void Init();
  void InitState();
  void Save();
  
  inline CalibrationData* mutable_calibration_data() {
    return &data_.calibration_data;
  }

  inline void ToggleFrequencyLocking() {
    data_.state.frequency_locked = !data_.state.frequency_locked;
  }

  void SwitchModeOption();
  inline uint8_t ModeOption() {
    return data_.state.mode_option;
  }

  void SwitchWaveformExciterOption();
  inline uint8_t WaveformExciterOption() {
    return data_.state.waveform_exciter_option;
  }

  void SwitchChordTableOption();
  inline uint8_t ChordTableOption() {
    return data_.state.chord_table_option;
  }

  void SwitchStrumHoldOption();
  inline uint8_t StrumHoldOption() {
    return data_.state.strum_hold_option;
  }

  inline State* mutable_state() {
    return &data_.state;
  }

  inline const State& state() const {
    return data_.state;
  }
  
  // True when no calibration data has been found on flash sector 1, that is
  // to say when the module has just been flashed.
  inline bool freshly_baked() const {
    return freshly_baked_;
  }
  
 private:
  bool freshly_baked_;
  SettingsData data_;
  uint16_t version_token_;
  
  DISALLOW_COPY_AND_ASSIGN(Settings);
};

}  // namespace rings

#endif  // RINGS_SETTINGS_H_
