// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' four TRIPLE models merged into one slot.

#include "plaits/dsp/engine2/triple_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/engine2/triple_engine_data.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' interval lookup, arithmetic and all:
//   detune_1 = intervals[p >> 9]
//   detune_2 = intervals[((p >> 8) + 1) >> 1]
//   xfade    = p << 8            (as a uint16)
// The crossfade weight at the knob centre is 255/256, not 1, which is exactly
// why unison is reachable there. Tidying this breaks that.
inline float LadderDetune(float knob) {
  const int p = static_cast<int>(knob * 32767.0f);
  int i1 = p >> 9;
  int i2 = ((p >> 8) + 1) >> 1;
  CONSTRAIN(i1, 0, kTripleNumIntervals - 1);
  CONSTRAIN(i2, 0, kTripleNumIntervals - 1);
  const float xfade = static_cast<float>((p << 8) & 0xffff) * \
      (1.0f / 65536.0f);
  const float a = kTripleIntervals[i1];
  const float b = kTripleIntervals[i2];
  return a + (b - a) * xfade;
}

}  // namespace

void TripleEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  for (int i = 0; i < kTripleNumVoices; ++i) {
    voice_[i].Init();
  }
  Reset();
}

void TripleEngine::Reset() {
  for (int i = 0; i < kTripleNumVoices; ++i) {
    voice_[i].Init();
    sine_phase_[i] = 0.0f;
  }
  dc_in_ = 0.0f;
  dc_out_ = 0.0f;
  dc_aux_in_ = 0.0f;
  dc_aux_out_ = 0.0f;
}

void TripleEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Reset();
  }

  const float root = NoteToFrequency(parameters.note);
  float frequency[kTripleNumVoices];
  frequency[0] = root;
  frequency[1] = NoteToFrequency(
      parameters.note + LadderDetune(parameters.harmonics));
  frequency[2] = NoteToFrequency(
      parameters.note + LadderDetune(parameters.timbre));

  // MORPH: square -> saw -> triangle over the first three quarters, then a
  // crossfade to sine.
  const float shaped_morph = min(
      parameters.morph / kTripleSineRegion, 1.0f);
  const float waveshape = 1.0f - shaped_morph;
  const float sine_amount = parameters.morph <= kTripleSineRegion
      ? 0.0f
      : (parameters.morph - kTripleSineRegion) / (1.0f - kTripleSineRegion);

  // MACRO narrows pulse width and slope asymmetry. Minimum equals stock, so
  // the detent and everything below it are Braids' symmetric waveforms.
  const float shape = ApplyMacro(0.5f, 0.5f, 0.95f, parameters.macro);
  const float pw = 1.0f - shape;

  float voice_buffer[kMaxBlockSize];

  for (size_t i = 0; i < size; ++i) {
    out[i] = 0.0f;
    aux[i] = 0.0f;
  }

  for (int v = 0; v < kTripleNumVoices; ++v) {
    voice_[v].Render(frequency[v], pw, waveshape, voice_buffer, size);
    for (size_t i = 0; i < size; ++i) {
      float sample = voice_buffer[i];
      if (sine_amount > 0.0f) {
        sine_phase_[v] += frequency[v];
        if (sine_phase_[v] >= 1.0f) {
          sine_phase_[v] -= 1.0f;
        }
        // MACRO is deliberately INERT here: the phase skew that would shape a
        // sine is only C-zero, falls at -12 dB/oct past ~10*f0 with no BLEP,
        // and would run on all three voices at once. Cheapest honest answer,
        // and symmetric with the saw region already being inert.
        const float sine = Sine(sine_phase_[v]);
        sample += (sine - sample) * sine_amount;
      }
      out[i] += sample * kTripleVoiceGain;
      if (v == 0) {
        // AUX carries the undetuned root alone, which gives a clean pitch
        // reference against the beating mix.
        aux[i] = sample;
      }
    }
  }

  // The narrow-pulse end of MACRO carries a large DC term by construction --
  // the same thing sub-oscillator hits, and the audio-health gate rejects it.
  // The corner sits far below anything three detuned voices produce.
  for (size_t i = 0; i < size; ++i) {
    const float mixed = out[i];
    dc_out_ = mixed - dc_in_ + kTripleDcPole * dc_out_;
    dc_in_ = mixed;
    out[i] = dc_out_;

    const float root_voice = aux[i];
    dc_aux_out_ = root_voice - dc_aux_in_ + kTripleDcPole * dc_aux_out_;
    dc_aux_in_ = root_voice;
    aux[i] = dc_aux_out_;
  }
}

}  // namespace plaits
