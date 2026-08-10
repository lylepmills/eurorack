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
// 8x8x3 wave terrain.
//
// OUT: an 8-point (x, y, z) interpolation of the 3-D wavetable at the wave
// phase, differentiated and gain-scaled. AUX: a 5-bit bitcrush of OUT. In
// stereo mode, OUT/AUX become L/R: the wavetable is evaluated once and the
// right channel is a short all-pass phase rotation of the left. This preserves
// the interpolated spectrum without doubling the 8-wave Hermite read.

#ifndef PLAITS_DSP_ENGINE_WAVETABLE_ENGINE_H_
#define PLAITS_DSP_ENGINE_WAVETABLE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/wavetable_oscillator.h"

namespace plaits {

class WavetableEngine : public Engine {
 public:
  WavetableEngine() { }
  ~WavetableEngine() { }
  
  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data);
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_WAVETABLE; }
  virtual bool hard_sync_capable() const { return true; }
  virtual bool linear_tzfm_capable() const { return true; }
  virtual bool fast_fm_capable() const { return true; }

 private:
  template<bool process_hard_sync>
  void RenderInternal(
      const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);

  float ReadCell(
      int x0, int x1, int y0, int y1, int z0, int z1,
      float x_fractional, float y_fractional, float z_fractional,
      int phase_integral, float phase_fractional);
   
  float phase_;
  
  float x_pre_lp_;
  float y_pre_lp_;
  float z_pre_lp_;
  
  float x_lp_;
  float y_lp_;
  float z_lp_;

  float previous_x_;
  float previous_y_;
  float previous_z_;
  float previous_f0_;
  
  // Maps a (bank, X, Y) coordinate to a waveform index.
  // This allows all waveforms to be reshuffled by the user to create new maps.
  const int16_t** wave_map_;
  
  Differentiator diff_out_;
  StereoPhaseAllpass<7> stereo_allpass_;

  DISALLOW_COPY_AND_ASSIGN(WavetableEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_WAVETABLE_ENGINE_H_
