// Copyright 2026 Dirk Hoppmann.
// SPDX-License-Identifier: MIT
//
// Modified from Chords (mutable-instruments/chords@1.0.0) for Plaits Lab.
// The original copyright and license notice follow.

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
// Supersaw Chords: the Chords chord bank driven by a detuned sawtooth unison
// stack per chord tone, in place of the original divide-down organ and
// wavetable pair. The chord is voiced in root position, without inversion.
//
// Each of the four chord tones runs up to kSupersawUnison band-limited saws
// spread unevenly around the tone's pitch. MORPH sets that spread, so the
// model runs from a tight single-timbre stack to a wide supersaw.
//
// OUT: the whole chord. AUX: the root voice, boosted.
// Stereo: ranks below the centre pitch go left and ranks above go right, so
// the detune spread itself becomes the stereo image.

#ifndef PLAITS_DSP_ENGINE2_CHORDS_SUPERSAW_ENGINE_H_
#define PLAITS_DSP_ENGINE2_CHORDS_SUPERSAW_ENGINE_H_

#include "plaits/dsp/chords/chord_bank.h"
#include "plaits/dsp/engine/engine.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/polyblep.h"

namespace plaits {

// Saws per chord tone. The chord bank's four notes run this many each, so the
// whole model is kChordNumNotes * kSupersawUnison independent oscillators.
const int kSupersawUnison = 3;

// A single band-limited sawtooth. One polyBLEP correction per wrap, carried
// into the next block through next_sample_, and per-block interpolation of
// both frequency and gain so that detune and stack-size moves stay quiet.
// The voice can also morph sine -> saw -> square, computing only the two shapes
// either side of a position. The model fixes it to the plain saw (see
// kSupersawFixedShape in the .cc), so that morph is not on a control.
enum SupersawShape {
  // Plain saw: no shape arithmetic at all beyond the ramp itself. The cheapest
  // shape, and the one the model is fixed to.
  SUPERSAW_SHAPE_SAW,
  SUPERSAW_SHAPE_SINE_TO_SAW,
  SUPERSAW_SHAPE_SAW_TO_SQUARE
};

class SupersawVoice {
 public:
  SupersawVoice() { }
  ~SupersawVoice() { }

  inline void Init() {
    phase_ = 0.0f;
    next_sample_ = 0.0f;
    frequency_ = 0.001f;
    gain_ = 0.0f;
  }

  inline void set_phase(float phase) {
    phase_ = phase;
  }

  inline void Render(
      SupersawShape shape,
      float amount,
      float frequency,
      float gain,
      float* out,
      size_t size) {
    CONSTRAIN(frequency, 1.0e-7f, 0.49f);

    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
    stmlib::ParameterInterpolator gain_modulation(&gain_, gain, size);

    float phase = phase_;
    float next_sample = next_sample_;

    if (shape == SUPERSAW_SHAPE_SAW) {
      while (size--) {
        float this_sample = next_sample;
        next_sample = 0.0f;

        const float f = fm.Next();
        const float g = gain_modulation.Next();

        phase += f;
        if (phase >= 1.0f) {
          phase -= 1.0f;
          // The ramp falls from +g to -g at the wrap: a step of -2g.
          const float t = phase / f;
          const float discontinuity = -2.0f * g;
          this_sample += stmlib::ThisBlepSample(t) * discontinuity;
          next_sample += stmlib::NextBlepSample(t) * discontinuity;
        }
        next_sample += (2.0f * phase - 1.0f) * g;
        *out++ += this_sample;
      }
    } else if (shape == SUPERSAW_SHAPE_SINE_TO_SAW) {
      const float sine_amount = 1.0f - amount;
      while (size--) {
        float this_sample = next_sample;
        next_sample = 0.0f;

        const float f = fm.Next();
        const float g = gain_modulation.Next();

        phase += f;
        if (phase >= 1.0f) {
          phase -= 1.0f;
          // Only the saw half of the mix steps at the wrap; the sine reaches
          // zero from both sides and stays continuous through it.
          const float t = phase / f;
          const float discontinuity = -2.0f * g * amount;
          this_sample += stmlib::ThisBlepSample(t) * discontinuity;
          next_sample += stmlib::NextBlepSample(t) * discontinuity;
        }
        const float x = 2.0f * phase - 1.0f;
        const float folded = x < 0.0f ? -x : x;
        // 4x(1-|x|) is one cycle of a parabolic sine: about 3% third harmonic,
        // four operations, and no table read competing with instruction fetch.
        const float sine = 4.0f * x * (1.0f - folded);
        next_sample += (x * amount + sine * sine_amount) * g;
        *out++ += this_sample;
      }
    } else {
      // A square is one saw minus the same saw shifted half a cycle, so this
      // morph is saw(p) - amount * saw(p + 1/2). The shifted ramp is derived
      // from phase once per block rather than carried as state: both advance by
      // the same increment inside the block, so they cannot drift apart. Two
      // float compares per sample, one per ramp -- compares stall this core,
      // and testing the half-way point on the main ramp instead needed four.
      float aux_phase = phase + 0.5f;
      if (aux_phase >= 1.0f) {
        aux_phase -= 1.0f;
      }
      while (size--) {
        float this_sample = next_sample;
        next_sample = 0.0f;

        const float f = fm.Next();
        const float g = gain_modulation.Next();

        phase += f;
        if (phase >= 1.0f) {
          phase -= 1.0f;
          const float t = phase / f;
          const float discontinuity = -2.0f * g;
          this_sample += stmlib::ThisBlepSample(t) * discontinuity;
          next_sample += stmlib::NextBlepSample(t) * discontinuity;
        }
        aux_phase += f;
        if (aux_phase >= 1.0f) {
          aux_phase -= 1.0f;
          // The subtracted ramp steps the other way, scaled by the morph.
          const float t = aux_phase / f;
          const float discontinuity = 2.0f * g * amount;
          this_sample += stmlib::ThisBlepSample(t) * discontinuity;
          next_sample += stmlib::NextBlepSample(t) * discontinuity;
        }
        next_sample += ((2.0f * phase - 1.0f)
            - amount * (2.0f * aux_phase - 1.0f)) * g;
        *out++ += this_sample;
      }
    }

    phase_ = phase;
    next_sample_ = next_sample;
  }

#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  // Audio-rate FM for the plain saw (the model's fixed shape). scale[] holds
  // a per-sample multiplier on the interpolated frequency. A negative value
  // runs the ramp backwards for through-zero FM: it then wraps at 0 instead
  // of 1, and the step there rises by 2g instead of falling.
  inline void RenderSawModulated(
      float frequency,
      float gain,
      const float* scale,
      float* out,
      size_t size) {
    CONSTRAIN(frequency, 1.0e-7f, 0.49f);

    stmlib::ParameterInterpolator fm(&frequency_, frequency, size);
    stmlib::ParameterInterpolator gain_modulation(&gain_, gain, size);

    float phase = phase_;
    float next_sample = next_sample_;

    for (size_t i = 0; i < size; ++i) {
      float this_sample = next_sample;
      next_sample = 0.0f;

      float f = fm.Next() * scale[i];
      CONSTRAIN(f, -0.49f, 0.49f);
      const float g = gain_modulation.Next();

      phase += f;
      if (phase >= 1.0f) {
        phase -= 1.0f;
        const float t = phase / f;
        const float discontinuity = -2.0f * g;
        this_sample += stmlib::ThisBlepSample(t) * discontinuity;
        next_sample += stmlib::NextBlepSample(t) * discontinuity;
      } else if (phase < 0.0f) {
        phase += 1.0f;
        const float t = (phase - 1.0f) / f;
        const float discontinuity = 2.0f * g;
        this_sample += stmlib::ThisBlepSample(t) * discontinuity;
        next_sample += stmlib::NextBlepSample(t) * discontinuity;
      }
      next_sample += (2.0f * phase - 1.0f) * g;
      *out++ += this_sample;
    }

    phase_ = phase;
    next_sample_ = next_sample;
  }
#endif

 private:
  float phase_;
  float next_sample_;
  float frequency_;
  float gain_;
};

class ChordsSupersawEngine : public Engine {
 public:
  ChordsSupersawEngine() { }
  ~ChordsSupersawEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return true; }
  // No HardSync() hook: the bounded sync fallback follows its reset with a
  // synthetic TRIGGER_RISING_EDGE, and the trigger handler already restarts
  // every oscillator from the fixed scatter in ScatterPhases().

 private:
  void ScatterPhases();

  SupersawVoice voice_[kChordNumNotes][kSupersawUnison];
  ChordBank chords_;

  float morph_lp_;
  float timbre_lp_;
  int active_unison_;

  DISALLOW_COPY_AND_ASSIGN(ChordsSupersawEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_CHORDS_SUPERSAW_ENGINE_H_
