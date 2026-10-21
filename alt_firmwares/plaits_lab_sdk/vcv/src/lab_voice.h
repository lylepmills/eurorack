// Copyright 2026 Matt Policastro.
// SPDX-License-Identifier: MIT
//
// One Plaits Lab engine behind the firmware Voice's control path, without
// Rack: panel controls and jack voltages in, 12-sample blocks of 48 kHz audio
// out in output-jack volts. The engine is chosen at build time (sources.py).

#ifndef PLAITS_LAB_VCV_LAB_VOICE_H_
#define PLAITS_LAB_VCV_LAB_VOICE_H_

#include <cstddef>

namespace plaits_lab_vcv {

const size_t kBlockSize = 12;
const float kSampleRate = 48000.0f;

// Panel state, in the units of plaits::Patch.
struct Controls {
  float note = 60.0f;  // FREQ: MIDI note
  float harmonics = 0.5f;
  float timbre = 0.5f;
  float morph = 0.5f;
  float macro = 0.5f;  // neutral for stock models
  float frequency_modulation_amount = 0.0f;  // attenuverters, -1 to 1
  float timbre_modulation_amount = 0.0f;
  float morph_modulation_amount = 0.0f;
  float decay = 0.5f;  // LPG, for engines that do not envelope themselves
  float lpg_colour = 0.5f;
  // For an engine that envelopes itself: run it untriggered and let Plaits'
  // LPG articulate it on TRIG instead, so DECAY and COLOUR apply.
  bool plaits_lpg = false;
};

// Jack voltages, and whether each jack is patched.
struct Inputs {
  float v_oct = 0.0f;
  float fm = 0.0f;
  float harmonics = 0.0f;
  float timbre = 0.0f;
  float morph = 0.0f;
  float trigger = 0.0f;
  float level = 0.0f;
  bool fm_patched = false;
  bool timbre_patched = false;
  bool morph_patched = false;
  bool trigger_patched = false;
  bool level_patched = false;
};

class LabVoice {
 public:
  LabVoice();
  ~LabVoice();

  // Renders kBlockSize samples of OUT and AUX, in volts.
  void Render(const Controls& controls, const Inputs& inputs, float* out, float* aux);

  static const char* name();

 private:
  struct State;
  State* state_;

  LabVoice(const LabVoice&);
  LabVoice& operator=(const LabVoice&);
};

}  // namespace plaits_lab_vcv

#endif  // PLAITS_LAB_VCV_LAB_VOICE_H_
