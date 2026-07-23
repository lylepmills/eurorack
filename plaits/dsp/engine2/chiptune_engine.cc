// Copyright 2021 Emilie Gillet.
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
// Chiptune waveforms with arpeggiator.

#include "plaits/dsp/engine2/chiptune_engine.h"

#include <algorithm>

namespace plaits {

using namespace std;
using namespace stmlib;

const uint8_t kRegisterVoicings[9][kChordNumVoices] = {
  { 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 0 },
  { 0, 1, 0, 0, 0 },
  { 0, 0, 0, 0, 1 },
  { 0, 0, 0, 0, 0 },  // Stock voicing is handled separately.
  { 0, 0, 0, 1, 1 },
  { 0, 0, 1, 1, 2 },
  { 0, 1, 1, 2, 2 },
  { 0, 1, 2, 3, 4 },
};

// Stereo positions of the chord voices: root centered, outer voices widest.
const float kStereoVoicePan[kChordNumVoices] = {
  0.5f, 0.2f, 0.8f, 0.05f, 0.95f
};

void ChiptuneEngine::Init(BufferAllocator* allocator) {
  bass_.Init();
  for (int i = 0; i < kChordNumNotes; ++i) {
    voice_[i].Init();
  }
  
  chords_.Init(allocator);
  
  arpeggiator_.Init();
  
  arpeggiator_pattern_selector_.Init(12, 0.075f, false);
  register_spread_selector_.Init(9, 0.075f, false);
  
  envelope_shape_ = NO_ENVELOPE;
  envelope_state_ = 0.0f;
  aux_envelope_amount_ = 0.0f;

  arp_pan_flip_ = false;
  StereoPanGains(0.2f, &arp_pan_left_, &arp_pan_right_);
}

void ChiptuneEngine::Reset() {
  chords_.Reset();
}

void ChiptuneEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = NoteToFrequency(parameters.note);
  const float shape = parameters.morph * 0.995f;
  const bool clocked = !(parameters.trigger & TRIGGER_UNPATCHED);
  const int register_mode = register_spread_selector_.Process(
      parameters.macro);
  float root_transposition = 1.0f;
  
  *already_enveloped = clocked;
  
  if (clocked) {
    if (parameters.trigger & TRIGGER_RISING_EDGE) {
      chords_.set_chord(parameters.harmonics, parameters.chord_set_option);
      chords_.Sort();

      int pattern = arpeggiator_pattern_selector_.Process(parameters.timbre);
      arpeggiator_.set_mode(ArpeggiatorMode(pattern / 3));
      arpeggiator_.set_range(1 << (pattern % 3));
      arpeggiator_.Clock(chords_.num_notes());
      // Each clock steps to a new arpeggiated note: alternate its stereo
      // position. Updated in mono too, so that enabling stereo mid-pattern
      // continues the ping-pong.
      arp_pan_flip_ = !arp_pan_flip_;
      StereoPanGains(
          arp_pan_flip_ ? 0.8f : 0.2f, &arp_pan_left_, &arp_pan_right_);
      envelope_state_ = 1.0f;
    }
    const float octave = float(1 << arpeggiator_.octave());
    const int arpeggiator_note = arpeggiator_.note();
    const float register_transposition = register_mode == 4
        ? octave
        : float(1 << kRegisterVoicings[register_mode][arpeggiator_note]);
    const float note_f0 = f0 * chords_.sorted_ratio(
        arpeggiator_note) * register_transposition;
    root_transposition = octave;
    if (parameters.stereo) {
      float temp[kMaxBlockSize];
      voice_[0].Render(note_f0, shape, temp, size);
      for (size_t j = 0; j < size; ++j) {
        out[j] = temp[j] * arp_pan_left_;
        aux[j] = temp[j] * arp_pan_right_;
      }
    } else {
      voice_[0].Render(note_f0, shape, out, size);
    }
  } else {
    float ratios[kChordNumVoices];
    float amplitudes[kChordNumVoices];

    chords_.set_chord(parameters.harmonics, parameters.chord_set_option);
    chords_.ComputeChordInversion(parameters.timbre, ratios, amplitudes);

    if (register_mode != 4) {
      float compact_ratios[kChordNumVoices];
      for (int voice = 0; voice < kChordNumVoices; ++voice) {
        float ratio = ratios[voice];
        while (ratio < 0.5f) {
          ratio *= 2.0f;
        }
        while (ratio >= 1.0f) {
          ratio *= 0.5f;
        }
        compact_ratios[voice] = ratio;
      }

      for (int voice = 0; voice < kChordNumVoices; ++voice) {
        int rank = 0;
        for (int other = 0; other < kChordNumVoices; ++other) {
          if (compact_ratios[other] < compact_ratios[voice] || \
              (compact_ratios[other] == compact_ratios[voice] && \
                  other < voice)) {
            ++rank;
          }
        }
        ratios[voice] = compact_ratios[voice] * float(
            1 << kRegisterVoicings[register_mode][rank]);
      }
    }

    for (int j = 1; j < kChordNumVoices; j += 2) {
      amplitudes[j] = -amplitudes[j];
    }
  
    fill(&out[0], &out[size], 0.0f);
    if (parameters.stereo) {
      fill(&aux[0], &aux[size], 0.0f);
      float temp[kMaxBlockSize];
      for (int voice = 0; voice < kChordNumVoices; ++voice) {
        const float voice_f0 = f0 * ratios[voice];
        float pan_left, pan_right;
        StereoPanGains(kStereoVoicePan[voice], &pan_left, &pan_right);
        const float gain_left = amplitudes[voice] * pan_left;
        const float gain_right = amplitudes[voice] * pan_right;
        voice_[voice].Render(voice_f0, shape, temp, size);
        for (size_t j = 0; j < size; ++j) {
          out[j] += temp[j] * gain_left;
          aux[j] += temp[j] * gain_right;
        }
      }
    } else {
      for (int voice = 0; voice < kChordNumVoices; ++voice) {
        const float voice_f0 = f0 * ratios[voice];
        voice_[voice].Render(voice_f0, shape, aux, size);
        for (size_t j = 0; j < size; ++j) {
          out[j] += aux[j] * amplitudes[voice];
        }
      }
    }
  }

  if (parameters.stereo) {
    // The bass stays centered, and the envelope is applied to each component
    // before it reaches the two channels, like in mono.
    float temp[kMaxBlockSize];
    bass_.Render(f0 * 0.5f * root_transposition, temp, size);
    float bass_left, bass_right;
    StereoPanGains(0.5f, &bass_left, &bass_right);
    if (envelope_shape_ != NO_ENVELOPE) {
      const float shape = fabsf(envelope_shape_);
      const float decay = 1.0f - \
          2.0f / kSampleRate * SemitonesToRatio(60.0f * shape) * shape;
      float aux_envelope_amount = envelope_shape_ * 20.0f;
      CONSTRAIN(aux_envelope_amount, 0.0f, 1.0f);

      for (size_t i = 0; i < size; ++i) {
        ONE_POLE(aux_envelope_amount_, aux_envelope_amount, 0.01f);
        envelope_state_ *= decay;
        const float bass = temp[i] * \
            (1.0f + aux_envelope_amount_ * (envelope_state_ - 1.0f));
        out[i] = out[i] * envelope_state_ + bass * bass_left;
        aux[i] = aux[i] * envelope_state_ + bass * bass_right;
      }
    } else {
      for (size_t i = 0; i < size; ++i) {
        out[i] += temp[i] * bass_left;
        aux[i] += temp[i] * bass_right;
      }
    }
  } else {
    // Render bass note.
    bass_.Render(f0 * 0.5f * root_transposition, aux, size);

    // Apply envelope if necessary.
    if (envelope_shape_ != NO_ENVELOPE) {
      const float shape = fabsf(envelope_shape_);
      const float decay = 1.0f - \
          2.0f / kSampleRate * SemitonesToRatio(60.0f * shape) * shape;
      float aux_envelope_amount = envelope_shape_ * 20.0f;
      CONSTRAIN(aux_envelope_amount, 0.0f, 1.0f);

      for (size_t i = 0; i < size; ++i) {
        ONE_POLE(aux_envelope_amount_, aux_envelope_amount, 0.01f);
        envelope_state_ *= decay;
        out[i] *= envelope_state_;
        aux[i] *= 1.0f + aux_envelope_amount_ * (envelope_state_ - 1.0f);
      }
    }
  }
}

}  // namespace plaits
