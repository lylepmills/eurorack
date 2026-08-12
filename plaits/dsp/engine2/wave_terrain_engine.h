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
// Wave terrain synthesis - a 2D function evaluated along an elliptical path of
// adjustable center and excentricity.
//
// This implementation initially used pre-computed terrains stored in flash
// memory, but even at a poor resolution of 64x64 with 8-bit samples, this
// takes 4kb per terrain! It turned out that directly evaluating the terrain
// function on the fly uses less flash, but is also faster than bicubic
// interpolation of the terrain data.
//
// OUT: the terrain height z at the trajectory point (x, y). AUX: Sine(y + z).
// In stereo mode, OUT/AUX become L/R using these same two stock outputs. The
// alternate Sine(y + z) projection is already a distinct view of the same
// trajectory, so a second terrain evaluation is unnecessary.

#ifndef PLAITS_DSP_ENGINE_WAVE_TERRAIN_ENGINE_H_
#define PLAITS_DSP_ENGINE_WAVE_TERRAIN_ENGINE_H_

#include <cmath>
#include <stdint.h>

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/atan.h"
#include "stmlib/dsp/rsqrt.h"

namespace plaits {

enum WaveTerrainType {
  WAVE_TERRAIN_FACTORY_0,
  WAVE_TERRAIN_FACTORY_1,
  WAVE_TERRAIN_FACTORY_2,
  WAVE_TERRAIN_FACTORY_3,
  WAVE_TERRAIN_FACTORY_4,
  WAVE_TERRAIN_FACTORY_5,
  WAVE_TERRAIN_FACTORY_6,
  WAVE_TERRAIN_FACTORY_7,
  WAVE_TERRAIN_CUSTOM,
  WAVE_TERRAIN_NATIVE
};

typedef float (*WaveTerrainFunction)(float x, float y);

// Bounded, errno-free primitives targeted by the trusted equation compiler.
// They mirror the hardware-probed diagnostic implementations and are inline so
// the linker retains only the operations used by the selected equations.
inline float TerrainEquationSin(float value) {
  return Sine(8.0f + value * 0.15915494309189535f);
}
inline float TerrainEquationCos(float value) {
  return Sine(8.25f + value * 0.15915494309189535f);
}
inline float TerrainEquationLog2(float x) {
  union { float f; uint32_t i; } u = { x };
  const float exponent = static_cast<float>((u.i >> 23) & 0xffu) - 127.0f;
  u.i = (u.i & 0x007fffffu) | 0x3f800000u;
  const float mantissa = u.f;
  return exponent + (-1.7417939f + (2.8212026f + (-1.4699568f +
      (0.44717955f - 0.056570851f * mantissa) * mantissa) * mantissa) *
      mantissa);
}
inline float TerrainEquationExp2(float x) {
  const float rounded = (x + 12582912.0f) - 12582912.0f;
  const float fraction = x - rounded;
  const float polynomial = 1.0f + fraction * (0.6931472f + fraction *
      (0.2402265f + fraction * (0.0555041f + fraction * 0.0096181f)));
  union { float f; int32_t i; } power;
  power.i = (static_cast<int32_t>(rounded) + 127) << 23;
  return polynomial * power.f;
}
inline float TerrainEquationExp(float x) {
  return TerrainEquationExp2(x * 1.4426950408889634f);
}
inline float TerrainEquationLog(float x) {
  return TerrainEquationLog2(x) * 0.6931471805599453f;
}
inline float TerrainEquationPower(float x, float exponent) {
  return TerrainEquationExp2(exponent * TerrainEquationLog2(x));
}
inline float TerrainEquationSqrt(float x) {
  return x > 0.0f ? x * stmlib::fast_rsqrt_accurate(x) : 0.0f;
}
inline float TerrainEquationAtan2(float y, float x) {
  return static_cast<float>(static_cast<int16_t>(stmlib::fast_atan2(y, x))) *
      (3.14159265358979323846f / 32768.0f);
}
inline float TerrainEquationAtan(float value) {
  return TerrainEquationAtan2(value, 1.0f);
}
inline float TerrainEquationSign(float value) {
  return value > 0.0f ? 1.0f : value < 0.0f ? -1.0f : 0.0f;
}
inline float TerrainEquationMin(float a, float b) { return a < b ? a : b; }
inline float TerrainEquationMax(float a, float b) { return a > b ? a : b; }
inline float TerrainEquationRound(float value) {
  return floorf(value + 0.5f);
}
inline float TerrainEquationBall(
    float x, float y, float xm, float ym, float width) {
  const float dx = x - xm;
  const float dy = y - ym;
  return TerrainEquationExp(-(dx * dx + dy * dy) / (width * width));
}

// A recipe-defined HARMONICS bank. Factory entries carry one of the eight
// factory type values and a NULL data pointer; custom entries carry
// WAVE_TERRAIN_CUSTOM and point at an independently rewritable 64 x 64 grid.
struct WaveTerrainBank {
  const uint8_t* types;
  const int8_t* const* data;
  const WaveTerrainFunction* functions;
  size_t size;
};
  
class WaveTerrainEngine : public Engine {
 public:
  WaveTerrainEngine() { }
  ~WaveTerrainEngine() { }
  
  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) {
    terrain_bank_ = NULL;
    user_terrain_ = (const int8_t*)(user_data);
  }
  virtual void LoadUserData(const uint8_t* user_data, size_t length) {
    if (user_data && length == 0) {
      terrain_bank_ = reinterpret_cast<const WaveTerrainBank*>(user_data);
      user_terrain_ = NULL;
    } else {
      LoadUserData(user_data);
    }
  }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_WAVE_TERRAIN; }
  virtual bool hard_sync_capable() const { return true; }
  virtual bool linear_tzfm_capable() const { return true; }

 private:
  float FactoryTerrain(float x, float y, int terrain_type);
  float Terrain(float x, float y, int terrain_index);
  
  FastSineOscillator path_;
  float offset_;
  float y_offset_;
  float terrain_;
  
  float* temp_buffer_;
  const WaveTerrainBank* terrain_bank_;
  const int8_t* user_terrain_;
  
  DISALLOW_COPY_AND_ASSIGN(WaveTerrainEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_WAVE_TERRAIN_ENGINE_H_
