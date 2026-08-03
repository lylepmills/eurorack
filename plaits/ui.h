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

#ifndef PLAITS_UI_H_
#define PLAITS_UI_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/hysteresis_quantizer.h"

#include "plaits/drivers/cv_adc.h"
#include "plaits/drivers/leds.h"
#include "plaits/drivers/normalization_probe.h"
#include "plaits/drivers/pots_adc.h"
#include "plaits/drivers/switches.h"
#include "plaits/dsp/voice.h"
#include "plaits/pitch_range.h"
#include "plaits/pot_controller.h"
#include "plaits/settings.h"

namespace plaits {

const int kNumNormalizedChannels = 5;
const int kProbeSequenceDuration = 32;

enum UiMode {
  UI_MODE_NORMAL,
  UI_MODE_DISPLAY_ALTERNATE_PARAMETERS,
  UI_MODE_DISPLAY_OCTAVE,
  UI_MODE_DISPLAY_DATA_TRANSFER_PROGRESS,
  UI_MODE_CHANGE_OPTIONS_PRE_RELEASE,
  UI_MODE_CHANGE_OPTIONS,
  UI_MODE_TEST,
  UI_MODE_ERROR
};

enum FactoryTestingCommand {
  FACTORY_TESTING_READ_POT,
  FACTORY_TESTING_READ_CV,
  FACTORY_TESTING_READ_GATE,
  FACTORY_TESTING_GENERATE_TEST_SIGNAL,
  FACTORY_TESTING_CALIBRATE,
  FACTORY_TESTING_READ_NORMALIZATION,
};

class Ui {
 public:
  Ui() { }
  ~Ui() { }

  void Init(Patch* patch, Modulations* modulations, Settings* settings);

  void Poll();

  void set_active_engine(int active_engine) {
    active_engine_ = active_engine;
  }

  void DisplayDataTransferProgress(float progress) {
    mode_ = UI_MODE_DISPLAY_DATA_TRANSFER_PROGRESS;
    data_transfer_progress_ = progress;
    // Cut in half the animation time when the transfer is over or to report
    // an error.
    pwm_counter_ = progress == 1.0f || progress < 0.0f ? 1500 : 0;
  }

  inline bool test_mode() const {
    return mode_ == UI_MODE_TEST;
  }

  uint8_t HandleFactoryTestingRequest(uint8_t command);

#if PLAITS_CPU_PROBE
  // Probe builds turn the LEDs into a CPU meter; see plaits/cpu_probe.h.
  inline void DisplayCpuUsage(float usage) { cpu_usage_ = usage; }
#endif

 private:
  void UpdateLEDs();
  void ReadSwitches();
  void ProcessPotsHiddenParameters();
  void LoadState();
  void SaveState();
  void DetectNormalization();

  void Navigate(int button);
  uint32_t BankToColor(int bank);

  // CV calibration (PLAITS_BUILD_ENABLE_CALIBRATION builds only). Declared
  // unconditionally, and defined + called only under that gate: ui.h
  // deliberately does NOT include build_config.h, since every unit that reaches
  // it must join the hosted builder's recipe-config scope (plaits/makefile,
  // check_config_scope.py) — and a class whose shape depends on a macro that
  // only some units see would be an ODR trap. So the gate lives wholly in
  // ui.cc, and calibration state is carried OUTSIDE UiMode (see
  // calibration_step_) so a build without it keeps the mode switches, and their
  // generated code, exactly as they were.
  void StartCalibration();
  void CalibrateC1();
  void CalibrateC3();

  bool OptionInert(int index) const;
  void StepOptionIndex(int delta);

  void RealignPots() {
    for (int i = POTS_ADC_CHANNEL_FREQUENCY_POT;
         i <= POTS_ADC_CHANNEL_MORPH_POT; ++i) {
      pots_[i].Realign();
    }
    // The right navigation button also arms the FM attenuverter's legacy
    // extra-fine-tune target. A short navigation press must disarm it along
    // with the four main knobs; otherwise the next ordinary FM adjustment is
    // mistaken for hidden-parameter editing and opens the amber octave display.
    pots_[POTS_ADC_CHANNEL_FM_ATTENUVERTER].Realign();
  }

  UiMode mode_;

  CvAdc cv_adc_;
  PotsAdc pots_adc_;
  Leds leds_;
  Switches switches_;

  int ui_task_;
  int option_index_;

  float data_transfer_progress_;
  float fine_tune_;
  float transposition_;
  float octave_;
  float tuned_root_note_;
  float precision_anchor_note_;
  int previous_pitch_range_;
  EndpointCatchUp precision_catch_up_;
  EndpointCatchUp octave_catch_up_;
  DeferredValueSave precision_root_save_;
  Patch* patch_;
  Modulations* modulations_;
  NormalizationProbe normalization_probe_;
  PotController pots_[POTS_ADC_CHANNEL_LAST];
  float pitch_lp_;

  // Calibration state. Unconditional (a few bytes of RAM, no flash) for the
  // same reason as the methods above. calibration_step_ is 0 when the module is
  // not calibrating, 1 while waiting for the low note and 2 for the high one —
  // a separate variable rather than two more UiMode values, so that a build
  // without calibration compiles the mode switches unchanged (adding
  // enumerators would also trip -Wswitch under -Werror).
  uint8_t calibration_step_;
  float pitch_lp_calibration_;
  float cv_c1_;
  // Calibration is entered by holding a button through power-up, so that press
  // is still down when the procedure starts. Steps only accept presses once
  // both buttons have been seen up.
  bool calibration_armed_;

  Settings* settings_;

  int normalization_detection_count_;
  int normalization_detection_mismatches_[kNumNormalizedChannels];
  uint32_t normalization_probe_state_;

  int pwm_counter_;
  int press_time_[SWITCH_LAST];
  bool ignore_release_[SWITCH_LAST];

  int active_engine_;
  // Per-bank memory for banked navigation: the row last selected in each bank,
  // so changing bank restores it (design "B"). Persisted across power cycles via
  // the saved State. Indexed by bank; up to four banks.
  uint8_t bank_last_row_[4];
  // not to be confused with the octave setting (octave_) -
  // when frequency is locked (by being in octave switch mode)
  // but using manual aux crossfade, stores the last octave
  // chosen manually with the frequency pot or right-button + MORPH shortcut.
  uint8_t locked_octave_;
  // MORPH writes here while the right button is held and FREQUENCY has another
  // assignment. The quantized result is copied into locked_octave_; keeping the
  // raw control separate gives the nine octave steps normal hysteresis.
  float locked_octave_control_;
  bool locked_octave_gesture_armed_;
  bool editing_locked_octave_;
#if PLAITS_CPU_PROBE
  float cpu_usage_;
#endif

  stmlib::HysteresisQuantizer2 octave_quantizer_;

  static const CvAdcChannel normalized_channels_[kNumNormalizedChannels];

  DISALLOW_COPY_AND_ASSIGN(Ui);
};

}  // namespace plaits

#endif  // PLAITS_UI_H_
