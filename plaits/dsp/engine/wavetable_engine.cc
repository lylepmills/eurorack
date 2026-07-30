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

#include "plaits/dsp/engine/wavetable_engine.h"

#include <algorithm>

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

const int kNumBanks = 4;
const int kNumWavesPerBank = 64;
const int kNumWaves = 192;
const int kNumCustomWaves = 15;

const size_t kTableSize = 128;
const float kTableSizeF = float(kTableSize);

void WavetableEngine::Init(BufferAllocator* allocator) {
  phase_ = 0.0f;

  x_lp_ = 0.0f;
  y_lp_ = 0.0f;
  z_lp_ = 0.0f;
  
  x_pre_lp_ = 0.0f;
  y_pre_lp_ = 0.0f;
  z_pre_lp_ = 0.0f;

  previous_x_ = 0.0f;
  previous_y_ = 0.0f;
  previous_z_ = 0.0f;
  previous_f0_ = a0;

  diff_out_.Init();
  stereo_allpass_.Init();

  wave_map_ = allocator->Allocate<const int16_t*>(kNumWavesPerBank);
}

void WavetableEngine::Reset() {
  
}

void WavetableEngine::LoadUserData(const uint8_t* user_data) {
  for (int bank = 0; bank < kNumBanks; ++bank) {
    for (int wave = 0; wave < kNumWavesPerBank; ++wave) {
      int i = bank * kNumWavesPerBank + wave;

      int w = i;
      if (bank == kNumBanks - 1) {
        w = user_data ? user_data[wave] : (w * 101 % kNumWaves);
      }

      const int16_t* base = wav_integrated_waves;
      if (w >= kNumWaves) {
        base = (const int16_t*)(user_data + 64);
        w = min(w - kNumWaves, kNumCustomWaves);
      }
      wave_map_[i] = base + size_t(w) * (kTableSize + 4);
    }
  }
}

inline float Clamp(float x, float amount) {
  x = x - 0.5f;
  x *= amount;
  CONSTRAIN(x, -0.5f, 0.5f);
  x += 0.5f;
  return x;
}

inline float WavetableEngine::ReadCell(
    int x0, int x1, int y0, int y1, int z0, int z1,
    float x_fractional, float y_fractional, float z_fractional,
    int phase_integral, float phase_fractional) {
  const int16_t* waves[8] = {
    wave_map_[x0 + y0 * 8 + z0 * kNumWavesPerBank],
    wave_map_[x1 + y0 * 8 + z0 * kNumWavesPerBank],
    wave_map_[x0 + y1 * 8 + z0 * kNumWavesPerBank],
    wave_map_[x1 + y1 * 8 + z0 * kNumWavesPerBank],
    wave_map_[x0 + y0 * 8 + z1 * kNumWavesPerBank],
    wave_map_[x1 + y0 * 8 + z1 * kNumWavesPerBank],
    wave_map_[x0 + y1 * 8 + z1 * kNumWavesPerBank],
    wave_map_[x1 + y1 * 8 + z1 * kNumWavesPerBank],
  };
  float phase_samples[4];
  for (int tap = 0; tap < 4; ++tap) {
    const int p = phase_integral + tap;
    const float x00 = waves[0][p] +
        (waves[1][p] - waves[0][p]) * x_fractional;
    const float x10 = waves[2][p] +
        (waves[3][p] - waves[2][p]) * x_fractional;
    const float z0_sample = x00 + (x10 - x00) * y_fractional;
    const float x01 = waves[4][p] +
        (waves[5][p] - waves[4][p]) * x_fractional;
    const float x11 = waves[6][p] +
        (waves[7][p] - waves[6][p]) * x_fractional;
    const float z1_sample = x01 + (x11 - x01) * y_fractional;
    phase_samples[tap] =
        z0_sample + (z1_sample - z0_sample) * z_fractional;
  }
  return InterpolateWaveHermite(phase_samples, 0, phase_fractional);
}

void WavetableEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = NoteToFrequency(parameters.note);
  
  ONE_POLE(x_pre_lp_, parameters.timbre * 6.9999f, 0.2f);
  ONE_POLE(y_pre_lp_, parameters.morph * 6.9999f, 0.2f);
  ONE_POLE(z_pre_lp_, parameters.harmonics * 6.9999f, 0.05f);
  
  const float x = x_pre_lp_;
  const float y = y_pre_lp_;
  const float z = z_pre_lp_;
  
  const float quantization = min(max(z - 3.0f, 0.0f), 1.0f);
  const float phase_warp = 0.14f * (2.0f * parameters.macro - 1.0f);
  const float lp_coefficient = min(
      max(2.0f * f0 * (4.0f - 3.0f * quantization), 0.01f), 0.1f);
  
  MAKE_INTEGRAL_FRACTIONAL(x);
  MAKE_INTEGRAL_FRACTIONAL(y);
  MAKE_INTEGRAL_FRACTIONAL(z);
  
  x_fractional += quantization * (Clamp(x_fractional, 16.0f) - x_fractional);
  y_fractional += quantization * (Clamp(y_fractional, 16.0f) - y_fractional);
  z_fractional += quantization * (Clamp(z_fractional, 16.0f) - z_fractional);
  
  ParameterInterpolator x_modulation(
      &previous_x_, static_cast<float>(x_integral) + x_fractional, size);
  ParameterInterpolator y_modulation(
      &previous_y_, static_cast<float>(y_integral) + y_fractional, size);
  ParameterInterpolator z_modulation(
      &previous_z_, static_cast<float>(z_integral) + z_fractional, size);

  ParameterInterpolator f0_modulation(&previous_f0_, f0, size);
  
  while (size--) {
    const float f0 = f0_modulation.Next();
    const float gain = (1.0f / (f0 * 131072.0f)) * (0.95f - f0);
    const float cutoff = min(kTableSizeF * f0, 1.0f);
    
    ONE_POLE(x_lp_, x_modulation.Next(), lp_coefficient);
    ONE_POLE(y_lp_, y_modulation.Next(), lp_coefficient);
    ONE_POLE(z_lp_, z_modulation.Next(), lp_coefficient);
    
    const float x = x_lp_;
    const float y = y_lp_;
    const float z = z_lp_;

    MAKE_INTEGRAL_FRACTIONAL(x);
    MAKE_INTEGRAL_FRACTIONAL(y);
    MAKE_INTEGRAL_FRACTIONAL(z);

    phase_ += f0;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }
    
    const float warped_phase = phase_ + phase_warp * Sine(phase_);
    const float p = warped_phase * kTableSizeF;
    MAKE_INTEGRAL_FRACTIONAL(p);
    
    {
      int x0 = x_integral;
      int x1 = x_integral + 1;
      int y0 = y_integral;
      int y1 = y_integral + 1;
      int z0 = z_integral;
      int z1 = z_integral + 1;
      
      if (z0 >= 4) {
        z0 = 7 - z0;
      }
      if (z1 >= 4) {
        z1 = 7 - z1;
      }
      
      float mix = ReadCell(
          x0, x1, y0, y1, z0, z1,
          x_fractional, y_fractional, z_fractional,
          p_integral, p_fractional);
      mix = diff_out_.Process(cutoff, mix) * gain;
      *out++ = mix;
      if ((PLAITS_STEREO_WAVETABLE && parameters.stereo)) {
        *aux++ = stereo_allpass_.Process(mix);
      } else {
        *aux++ = static_cast<float>(static_cast<int>(mix * 32.0f)) / 32.0f;
      }
    }
  }
}

}  // namespace plaits
