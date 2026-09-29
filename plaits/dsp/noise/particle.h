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
// Random impulse train processed by a resonant filter.

#ifndef PLAITS_DSP_NOISE_PARTICLE_H_
#define PLAITS_DSP_NOISE_PARTICLE_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/copyable_svf.h"
#include "plaits/dsp/local_random.h"

namespace plaits {

class Particle {
 public:
  Particle() { }
  ~Particle() { }

  inline void Init() {
    pre_gain_ = 0.0f;
    filter_.Init();
  }
  
  inline void Render(
      bool sync,
      float density,
      float gain,
      float frequency,
      float spread,
      float q,
      float* out,
      float* aux,
      size_t size) {
    LocalRandom random;  // see local_random.h
    float u = random.GetFloat();
    if (sync) {
      u = density;
    }
    bool can_radomize_frequency = true;
    CopyableSvf filter = filter_;
    float pre_gain = pre_gain_;
    while (size--) {
      float s = 0.0f;
      if (u <= density) {
        s = u * gain;
        if (can_radomize_frequency) {
          const float u = 2.0f * random.GetFloat() - 1.0f;
          const float f = std::min(
              stmlib::SemitonesToRatio(spread * u) * frequency,
              0.25f);
          pre_gain = 0.5f / stmlib::Sqrt(q * f * stmlib::Sqrt(density));
          filter.set_f_q<stmlib::FREQUENCY_DIRTY>(f, q);
          // Keep the cutoff constant for this whole block.
          can_radomize_frequency = false;
        }
      }
      *aux++ += s;
      *out++ += filter.Process<stmlib::FILTER_MODE_BAND_PASS>(pre_gain * s);
      u = random.GetFloat();
    }
    filter_ = filter;
    pre_gain_ = pre_gain;
    random.Commit();
  }

  // alt firmware: stereo variant - the band-pass filtered particle is
  // multiply-accumulated into both channels with a pair of pan gains, and
  // the raw pulses are not sent anywhere.
  inline void RenderStereo(
      bool sync,
      float density,
      float gain,
      float frequency,
      float spread,
      float q,
      float left_gain,
      float right_gain,
      float* left,
      float* right,
      size_t size) {
    LocalRandom random;  // see local_random.h
    float u = random.GetFloat();
    if (sync) {
      u = density;
    }
    bool can_radomize_frequency = true;
    CopyableSvf filter = filter_;
    float pre_gain = pre_gain_;
    while (size--) {
      float s = 0.0f;
      if (u <= density) {
        s = u * gain;
        if (can_radomize_frequency) {
          const float u = 2.0f * random.GetFloat() - 1.0f;
          const float f = std::min(
              stmlib::SemitonesToRatio(spread * u) * frequency,
              0.25f);
          pre_gain = 0.5f / stmlib::Sqrt(q * f * stmlib::Sqrt(density));
          filter.set_f_q<stmlib::FREQUENCY_DIRTY>(f, q);
          // Keep the cutoff constant for this whole block.
          can_radomize_frequency = false;
        }
      }
      const float bp = filter.Process<stmlib::FILTER_MODE_BAND_PASS>(pre_gain * s);
      *left++ += left_gain * bp;
      *right++ += right_gain * bp;
      u = random.GetFloat();
    }
    filter_ = filter;
    pre_gain_ = pre_gain;
    random.Commit();
  }

 private:
  // A plain-struct filter (stmlib::Svf's arithmetic) so the render loops can
  // run it, and pre_gain_, from locals: as members they were reloaded and
  // stored every sample. See copyable_svf.h.
  float pre_gain_;
  CopyableSvf filter_;
  
  DISALLOW_COPY_AND_ASSIGN(Particle);
};

}  // namespace plaits

#endif  // PLAITS_DSP_NOISE_PARTICLE_H_
