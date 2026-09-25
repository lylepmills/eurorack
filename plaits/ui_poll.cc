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
// Ui::Poll, in its own translation unit so that it builds at the default -O2:
// the plaits makefile builds ui.o at -Os to save flash, but Poll runs in the
// audio interrupt every block, where -Os code on this uncached flash cost
// measurably more than stock Plaits' -O2 build. The rest of the UI (menus,
// LEDs, buttons, calibration) stays in ui.cc at -Os.

#include "plaits/ui.h"
#include "plaits/section_marks.h"

#include "stmlib/dsp/dsp.h"
#include "plaits/build_config.h"
#include "plaits/pitch_range.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void Ui::Poll() {
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  sync_input_.SetEnabled(patch_->model_cv_option == 4);
  modulations_->hard_sync = sync_input_.ReadEvents(kBlockSize);
#else
  modulations_->hard_sync = 0;
#endif
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  modulations_->frequency_audio_rate = false;
#endif
#if PLAITS_BUILD_FAST_FM
  if (audio_rate_fm_needed_) {
    ReadAudioRateFm(modulations_->frequency_audio, kBlockSize);
    modulations_->frequency_audio_rate = true;
  }
#endif
  for (int i = 0; i < POTS_ADC_CHANNEL_LAST; ++i) {
    pots_[i].ProcessControlRate(pots_adc_.float_value(PotsAdcChannel(i)));
  }

  float* destination = &modulations_->engine;
  for (int i = 0; i < CV_ADC_CHANNEL_LAST; ++i) {
    destination[i] = settings_->calibration_data(i).Transform(
        cv_adc_.float_value(CvAdcChannel(i)));
  }
  PLAITS_SECTION_MARK(SECTION_MARK_UI_INPUTS);
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  if (!modulations_->frequency_audio_rate) {
    // Slow TZFM and fast-mode fallback engines use the ordinary calibrated FM
    // reading, repeated across the block. This preserves LEVEL and stock ADC
    // scanning whenever the Fast FM preference is disabled.
    for (size_t i = 0; i < kBlockSize; ++i) {
      modulations_->frequency_audio[i] = modulations_->frequency;
    }
  }
#endif
#if PLAITS_BUILD_FAST_FM
  // The Level jack shares SDADC2 with FM and is intentionally unavailable.
  // Zeroing both fields also prevents a recipe whose saved level-input option
  // was "decay" from applying a phantom modulation.
  modulations_->level = 0.0f;
  modulations_->level_patched = false;
#endif

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
  PLAITS_UI_TASK_MARK(ui_task_);

  cv_adc_.Convert();
  pots_adc_.Convert();

  const int pitch_range = PitchRangeFromControl(octave_);

  // Selecting a range sweeps across the ranges in between, and each of them
  // rewrites patch_->note on the way past, so the pitch present when the
  // selection lands on octave switching is not the pitch the player was
  // hearing when they reached for the selector. Take the snapshot before this
  // tick's range change is handled, while patch_->note still holds the value
  // the previous tick left there.
  octave_root_snapshot_.Track(
      pots_[POTS_ADC_CHANNEL_HARMONICS_POT].editing_hidden_parameter(),
      previous_pitch_range_,
      patch_->note,
      tuned_root_note_);

  // Range transitions capture the MANUAL pitch only; V/OCT and FM live in the
  // modulation structure and are never baked into the tuned root. Each new
  // FREQUENCY role starts from its neutral value, then catches the physical
  // knob with the same endpoint-weighted response as Plaits' hidden params.
  if (pitch_range != previous_pitch_range_) {
    if (pitch_range == PITCH_RANGE_PRECISION) {
      // Fine tuning anchors on the SOUNDING pitch rather than the snapshot:
      // arriving from octave switching, that pitch includes the octave the
      // player selected, and moving the anchor off it would jump the tuning.
      precision_anchor_note_ = patch_->note;
      precision_catch_up_.Init(
          0.5f, 0.5f * transposition_ + 0.5f);
    } else if (pitch_range == PITCH_RANGE_OCTAVES) {
      tuned_root_note_ = octave_root_snapshot_.Root(patch_->note);
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

}  // namespace plaits
