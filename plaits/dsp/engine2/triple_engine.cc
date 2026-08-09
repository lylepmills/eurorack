// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' four TRIPLE models merged into one slot.

#include "plaits/dsp/engine2/triple_engine.h"
#include "plaits/build_config.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/engine2/triple_engine_data.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' interval lookup, arithmetic and all (macro_oscillator.cc:203-208):
//   detune_1 = intervals[p >> 9]
//   detune_2 = intervals[((p >> 8) + 1) >> 1]
//   xfade    = p << 8            (as a uint16)
//   detune   = detune_1 + ((detune_2 - detune_1) * xfade >> 16)
//
// That last `>>` is an arithmetic shift on an int, so the crossfade FLOORS: the
// module's detune is always a whole number of 1/128 semitones, never a point
// between two of them. The residue is load-bearing rather than rounding -- at
// the knob centre (p = 16383) it floors -4 + (4 * 65280 >> 16) to -1/128
// semitone = -0.781 cents on BOTH upper voices, which is the module's slow
// chorus at noon. Evaluating the same expression in float, as this did until
// the floor was restored, gives -0.012 cents and a hard unison.
//
// Braids' `intervals` table is int16_t in 1/128-semitone units, so the shift
// has to happen in those units. Every entry of kTripleIntervals is one of those
// integers over 128 and therefore an exact binary fraction, which scaling by
// 128 recovers bit-for-bit -- there is no rounding on the way in or out.
inline float LadderDetune(float knob) {
  const int p = static_cast<int>(knob * 32767.0f);
  int i1 = p >> 9;
  int i2 = ((p >> 8) + 1) >> 1;
  CONSTRAIN(i1, 0, kTripleNumIntervals - 1);
  CONSTRAIN(i2, 0, kTripleNumIntervals - 1);
  const int xfade = (p << 8) & 0xffff;
  const int detune_1 = static_cast<int>(kTripleIntervals[i1] * 128.0f);
  const int detune_2 = static_cast<int>(kTripleIntervals[i2] * 128.0f);
  const int detune = detune_1 + (((detune_2 - detune_1) * xfade) >> 16);
  return static_cast<float>(detune) * (1.0f / 128.0f);
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

  // A pulse of duty d sits at 2d - 1, so the narrow end of MACRO carries real
  // DC. It is subtracted here, in closed form, rather than filtered off the
  // mix: VariableShapeOscillator's naive sample averages
  // 0.5 * (1 - square_amount) + (1 - pw) * square_amount over a cycle, and its
  // output is 2s - 1, so the mean is exactly square_amount * (1 - 2 * pw).
  // At pw = 0.5 -- the whole lower half of MACRO, which is everything Braids
  // can reach -- that is bit-exactly zero, and it is zero again through the
  // saw, triangle and sine regions where square_amount is zero. So the port-
  // only axis pays for its own offset and the module's territory is untouched.
  // The one-pole blocker this replaced taxed every note in the bass instead.
  const float square_amount = max(waveshape - 0.5f, 0.0f) * 2.0f;

  for (int v = 0; v < kTripleNumVoices; ++v) {
    // The oscillator clamps pw against its own frequency before using it
    // (variable_shape_oscillator.h:101-105), so the offset has to be computed
    // from the clamped value or it would over-correct at the top of the range.
    float voice_pw = pw;
    if (frequency[v] >= kMaxFrequency) {
      voice_pw = 0.5f;
    } else {
      CONSTRAIN(voice_pw, frequency[v] * 2.0f, 1.0f - 2.0f * frequency[v]);
    }
    const float dc = square_amount * (1.0f - 2.0f * voice_pw);

#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    float voice_frequency_offset[kMaxBlockSize];
    const float voice_ratio = frequency[v] /
        (root > 1.0e-9f ? root : 1.0e-9f);
    if (parameters.frequency_offset) {
      for (size_t i = 0; i < size; ++i) {
        voice_frequency_offset[i] =
            parameters.frequency_offset[i] * voice_ratio;
      }
      voice_[v].RenderLinearFm(
          frequency[v],
          pw,
          waveshape,
          voice_frequency_offset,
          voice_buffer,
          size);
    } else {
#endif
      voice_[v].Render(frequency[v], pw, waveshape, voice_buffer, size);
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    }
#endif
    for (size_t i = 0; i < size; ++i) {
      float sample = voice_buffer[i] - dc;
      if (sine_amount > 0.0f) {
        float sine_frequency = frequency[v];
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
        if (parameters.frequency_offset) {
          sine_frequency += voice_frequency_offset[i];
          CONSTRAIN(sine_frequency, -0.49f, 0.49f);
        }
#endif
        sine_phase_[v] += sine_frequency;
        if (sine_phase_[v] >= 1.0f) {
          sine_phase_[v] -= 1.0f;
        } else if (sine_phase_[v] < 0.0f) {
          sine_phase_[v] += 1.0f;
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

  // No output filter of any kind, because RenderTriple has none.
}

}  // namespace plaits
