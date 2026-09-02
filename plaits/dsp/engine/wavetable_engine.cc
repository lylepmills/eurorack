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

#include "plaits/build_config.h"
#include "plaits/dsp/integrated_wavetable.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "plaits/resources.h"

#ifndef PLAITS_WAVETABLE_PRODUCTION_AUTOSWEEP
#define PLAITS_WAVETABLE_PRODUCTION_AUTOSWEEP 0
#endif

namespace plaits {

using namespace std;
using namespace stmlib;

const int kNumBanks = 4;
const int kNumWavesPerBank = 64;
const int kNumWaves = 192;
const int kNumCustomWaves = 15;

const size_t kTableSize = 128;
const float kTableSizeF = float(kTableSize);

#if PLAITS_WAVETABLE_PRODUCTION_AUTOSWEEP
// Autonomous hardware gate for recipe-defined banks. The ordinary builder
// never defines this flag, so these constants and branches are absent from
// shipping firmware. Four differently pitched windows sweep the full
// HARMONICS path while exercising neutral and corner TIMBRE/MORPH/MACRO
// settings. Silent settling gaps make a Core Audio capture self-synchronizing.
const uint32_t kWavetableAutosweepLeaderSamples = 6u * 48000u;
const uint32_t kWavetableAutosweepGapSamples = 3u * 24000u;
const uint32_t kWavetableAutosweepWindowSamples = 8u * 48000u;
const uint32_t kWavetableAutosweepSlotSamples =
    kWavetableAutosweepGapSamples + kWavetableAutosweepWindowSamples;
const uint32_t kWavetableAutosweepProfiles = 4u;
const uint32_t kWavetableAutosweepTrailerSamples = 6u * 48000u;
const uint32_t kWavetableAutosweepCycleSamples =
    kWavetableAutosweepLeaderSamples +
    kWavetableAutosweepProfiles * kWavetableAutosweepSlotSamples +
    kWavetableAutosweepTrailerSamples;

uint32_t wavetable_autosweep_samples = 0;
#endif

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
#if PLAITS_HAS_WAVETABLE_BANK
  wavetable_bank_ = NULL;
#endif

  wave_map_ = allocator->Allocate<const int16_t*>(kNumWavesPerBank);
}

void WavetableEngine::Reset() {
  
}

void WavetableEngine::LoadUserData(const uint8_t* user_data) {
#if PLAITS_HAS_WAVETABLE_BANK
  wavetable_bank_ = NULL;
#if !PLAITS_WAVETABLE_FACTORY_MASK
  // A compact shared bank containing only sampled/native entries never uses
  // the legacy 3-bank map. Keep this virtual method for Engine compatibility,
  // but do not name a factory wave bank: that lets --gc-sections reclaim every
  // unused bank when no other selected model needs it.
  (void) user_data;
  return;
#endif
#endif
  for (int bank = 0; bank < kNumBanks; ++bank) {
    for (int wave = 0; wave < kNumWavesPerBank; ++wave) {
      int i = bank * kNumWavesPerBank + wave;

      int w = i;
      if (bank == kNumBanks - 1) {
        w = user_data ? user_data[wave] : (w * 101 % kNumWaves);
      }

      if (w >= kNumWaves) {
        const int16_t* base = (const int16_t*)(user_data + 64);
        w = min(w - kNumWaves, kNumCustomWaves);
        wave_map_[i] = base + size_t(w) * (kTableSize + 4);
      } else {
        const int16_t* wave = FactoryIntegratedWavetable(w);
        // Customized banks never read omitted factory entries through this
        // legacy map. Give the allocator a valid fallback nevertheless.
        wave_map_[i] = wave ? wave : FactoryIntegratedWavetableBank(0);
      }
    }
  }
}

#if PLAITS_HAS_WAVETABLE_BANK
int WavetableEngine::BankAtPathIndex(int path_index) const {
  const int banks = static_cast<int>(wavetable_bank_->size);
  if (!wavetable_bank_->mirrored) {
    return min(max(path_index, 0), banks - 1);
  }
  const int path_size = banks * 2;
  path_index = min(max(path_index, 0), path_size - 1);
  return path_index < banks ? path_index : path_size - 1 - path_index;
}

float WavetableEngine::ReadDynamicIntegrated(
    int bank,
    int x0,
    int x1,
    int y0,
    int y1,
    float x_fractional,
    float y_fractional,
    int phase_integral,
  float phase_fractional) {
#if PLAITS_WAVETABLE_FACTORY_MASK
  const int type = wavetable_bank_->types[bank];
  const int cells[4] = {
    x0 + y0 * 8,
    x1 + y0 * 8,
    x0 + y1 * 8,
    x1 + y1 * 8,
  };
  float phase_samples[4];
  for (int tap = 0; tap < 4; ++tap) {
    const int p = phase_integral + tap;
    const int16_t* base = FactoryIntegratedWavetableBank(type);
    if (!base) return 0.0f;
    const int16_t* a = base + size_t(cells[0]) * (kTableSize + 4);
    const int16_t* b = base + size_t(cells[1]) * (kTableSize + 4);
    const int16_t* c = base + size_t(cells[2]) * (kTableSize + 4);
    const int16_t* d = base + size_t(cells[3]) * (kTableSize + 4);
    const float low = a[p] + (b[p] - a[p]) * x_fractional;
    const float high = c[p] + (d[p] - c[p]) * x_fractional;
    phase_samples[tap] = low + (high - low) * y_fractional;
  }
  return InterpolateWaveHermite(phase_samples, 0, phase_fractional);
#else
  (void) bank;
  (void) x0;
  (void) x1;
  (void) y0;
  (void) y1;
  (void) x_fractional;
  (void) y_fractional;
  (void) phase_integral;
  (void) phase_fractional;
  return 0.0f;
#endif
}

float WavetableEngine::ReadDynamicDirect(
    int bank,
    int x0,
    int x1,
    int y0,
    int y1,
    float x_fractional,
    float y_fractional,
    int phase_integral,
    float phase_fractional) {
  const int8_t* data = wavetable_bank_->data[bank];
  if (!data) return 0.0f;
  const int cells[4] = {
    x0 + y0 * 8,
    x1 + y0 * 8,
    x0 + y1 * 8,
    x1 + y1 * 8,
  };
  float phase_samples[4];
  for (int tap = 0; tap < 4; ++tap) {
    const int p = (phase_integral + tap) & (kTableSize - 1);
    const float a = data[cells[0] * kTableSize + p];
    const float b = data[cells[1] * kTableSize + p];
    const float c = data[cells[2] * kTableSize + p];
    const float d = data[cells[3] * kTableSize + p];
    const float low = a + (b - a) * x_fractional;
    const float high = c + (d - c) * x_fractional;
    phase_samples[tap] = low + (high - low) * y_fractional;
  }
  return InterpolateWaveHermite(phase_samples, 0, phase_fractional) *
      (1.0f / 127.0f);
}
#endif  // PLAITS_HAS_WAVETABLE_BANK

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
#if PLAITS_WAVETABLE_PRODUCTION_AUTOSWEEP
  EngineParameters automated = parameters;
  uint32_t position = wavetable_autosweep_samples;
  bool silent = true;
  int profile = 0;
  float harmonics = 0.0f;
  if (position >= kWavetableAutosweepLeaderSamples) {
    position -= kWavetableAutosweepLeaderSamples;
    const uint32_t profile_region =
        kWavetableAutosweepProfiles * kWavetableAutosweepSlotSamples;
    if (position < profile_region) {
      profile = static_cast<int>(position / kWavetableAutosweepSlotSamples);
      const uint32_t within_slot = position % kWavetableAutosweepSlotSamples;
      if (within_slot >= kWavetableAutosweepGapSamples) {
        silent = false;
        harmonics = static_cast<float>(
            within_slot - kWavetableAutosweepGapSamples) /
            static_cast<float>(kWavetableAutosweepWindowSamples - 1u);
        CONSTRAIN(harmonics, 0.0f, 1.0f);
      }
    }
  }
  static const float kNotes[kWavetableAutosweepProfiles] = {
    36.0f, 48.0f, 60.0f, 72.0f,
  };
  static const float kTimbres[kWavetableAutosweepProfiles] = {
    0.5f, 0.0f, 1.0f, 1.0f,
  };
  static const float kMorphs[kWavetableAutosweepProfiles] = {
    0.5f, 1.0f, 1.0f, 0.0f,
  };
  static const float kMacros[kWavetableAutosweepProfiles] = {
    0.5f, 0.5f, 0.0f, 1.0f,
  };
  automated.note = kNotes[profile];
  automated.timbre = kTimbres[profile];
  automated.morph = kMorphs[profile];
  automated.macro = kMacros[profile];
  automated.harmonics = harmonics;
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  automated.hard_sync = 0;
#endif
  automated.stereo = false;

  wavetable_autosweep_samples += static_cast<uint32_t>(size);
  while (wavetable_autosweep_samples >= kWavetableAutosweepCycleSamples) {
    wavetable_autosweep_samples -= kWavetableAutosweepCycleSamples;
  }
  RenderInternal<false>(automated, out, aux, size, already_enveloped);
  if (silent) {
    fill(out, out + size, 0.0f);
    fill(aux, aux + size, 0.0f);
  }
  return;
#endif
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  if (parameters.hard_sync) {
    RenderInternal<true>(parameters, out, aux, size, already_enveloped);
  } else {
#endif
    RenderInternal<false>(parameters, out, aux, size, already_enveloped);
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  }
#endif
}

template<bool process_hard_sync>
void WavetableEngine::RenderInternal(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  uint32_t hard_sync = process_hard_sync ? parameters.hard_sync : 0;
#else
  uint32_t hard_sync = 0;
#endif
#if PLAITS_HAS_WAVETABLE_BANK
  if (wavetable_bank_) {
    const float f0 = NoteToFrequency(parameters.note);
    const int num_banks = max(static_cast<int>(wavetable_bank_->size), 1);
    const int path_size = wavetable_bank_->mirrored
        ? num_banks * 2 : num_banks;

    ONE_POLE(x_pre_lp_, parameters.timbre * 6.9999f, 0.2f);
    ONE_POLE(y_pre_lp_, parameters.morph * 6.9999f, 0.2f);
    ONE_POLE(z_pre_lp_, parameters.harmonics * float(path_size - 1), 0.05f);

    const float quantization = min(
        max(parameters.harmonics * 7.0f - 3.0f, 0.0f), 1.0f);
    const float phase_warp = 0.14f * (2.0f * parameters.macro - 1.0f);
    const float lp_coefficient = min(
        max(2.0f * f0 * (4.0f - 3.0f * quantization), 0.01f), 0.1f);

    ParameterInterpolator x_modulation(&previous_x_, x_pre_lp_, size);
    ParameterInterpolator y_modulation(&previous_y_, y_pre_lp_, size);
    ParameterInterpolator z_modulation(&previous_z_, z_pre_lp_, size);
    ParameterInterpolator f0_modulation(&previous_f0_, f0, size);
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    const float* frequency_offset = parameters.frequency_offset;
#else
    const float* frequency_offset = NULL;
#endif

    while (size--) {
      float current_f0 = f0_modulation.Next();
      if (frequency_offset) {
        current_f0 += *frequency_offset++;
        CONSTRAIN(current_f0, -0.5f, 0.499999f);
      }
      const float absolute_f0 = max(fabsf(current_f0), 1.0e-7f);
      const float gain =
          (1.0f / (absolute_f0 * 131072.0f)) * (0.95f - absolute_f0);
      const float cutoff = min(kTableSizeF * absolute_f0, 1.0f);

      ONE_POLE(x_lp_, x_modulation.Next(), lp_coefficient);
      ONE_POLE(y_lp_, y_modulation.Next(), lp_coefficient);
      ONE_POLE(z_lp_, z_modulation.Next(), lp_coefficient);
      float x = x_lp_;
      float y = y_lp_;
      float z = z_lp_;
      CONSTRAIN(x, 0.0f, 6.9999f);
      CONSTRAIN(y, 0.0f, 6.9999f);
      CONSTRAIN(z, 0.0f, float(path_size - 1));
      MAKE_INTEGRAL_FRACTIONAL(x);
      MAKE_INTEGRAL_FRACTIONAL(y);
      MAKE_INTEGRAL_FRACTIONAL(z);
      x_fractional += quantization *
          (Clamp(x_fractional, 16.0f) - x_fractional);
      y_fractional += quantization *
          (Clamp(y_fractional, 16.0f) - y_fractional);
      z_fractional += quantization *
          (Clamp(z_fractional, 16.0f) - z_fractional);

      if (process_hard_sync) {
        if (hard_sync & 1) phase_ = 0.0f;
        hard_sync >>= 1;
      }
      phase_ += current_f0;
      if (phase_ >= 1.0f) phase_ -= 1.0f;
      else if (phase_ < 0.0f) phase_ += 1.0f;
      float warped_phase = phase_ + phase_warp * Sine(phase_);
      if (warped_phase < 0.0f) warped_phase += 1.0f;
      else if (warped_phase >= 1.0f) warped_phase -= 1.0f;
      const float p = warped_phase * kTableSizeF;
      MAKE_INTEGRAL_FRACTIONAL(p);

      const int x0 = static_cast<int>(x_integral);
      const int y0 = static_cast<int>(y_integral);
      const int z0 = static_cast<int>(z_integral);
      const int left_bank = BankAtPathIndex(z0);
      const int right_bank = BankAtPathIndex(
          min(z0 + 1, path_size - 1));
      const float left_weight = 1.0f - z_fractional;
      const float right_weight = z_fractional;
      const int bank_indices[2] = { left_bank, right_bank };
      const float bank_weights[2] = { left_weight, right_weight };
      float integrated = 0.0f;
      float direct = 0.0f;
      for (int side = 0; side < 2; ++side) {
        const int bank = bank_indices[side];
        const float weight = bank_weights[side];
        const int type = wavetable_bank_->types[bank];
        if (type <= WAVETABLE_BANK_FACTORY_3) {
          integrated += weight * ReadDynamicIntegrated(
              bank,
              x0,
              min(x0 + 1, 7),
              y0,
              min(y0 + 1, 7),
              x_fractional,
              y_fractional,
              p_integral,
              p_fractional);
        } else if (type == WAVETABLE_BANK_SAMPLED) {
          direct += weight * ReadDynamicDirect(
              bank,
              x0,
              min(x0 + 1, 7),
              y0,
              min(y0 + 1, 7),
              x_fractional,
              y_fractional,
              p_integral,
              p_fractional);
        } else if (type == WAVETABLE_BANK_NATIVE
            && wavetable_bank_->functions[bank]) {
          direct += weight * wavetable_bank_->functions[bank](
              warped_phase * 6.28318530717958648f,
              x * (1.0f / 7.0f),
              y * (1.0f / 7.0f));
        }
      }
      float mix = diff_out_.Process(cutoff, integrated) * gain + direct;
      CONSTRAIN(mix, -1.0f, 1.0f);
      *out++ = mix;
      if (PLAITS_STEREO_WAVETABLE && parameters.stereo) {
        *aux++ = stereo_allpass_.Process(mix);
      } else {
        *aux++ = static_cast<float>(static_cast<int>(mix * 32.0f)) / 32.0f;
      }
    }
    return;
  }
#endif  // PLAITS_HAS_WAVETABLE_BANK
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
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  const float* frequency_offset = parameters.frequency_offset;
#else
  const float* frequency_offset = NULL;
#endif
  
  while (size--) {
    float f0 = f0_modulation.Next();
    if (frequency_offset) {
      f0 += *frequency_offset++;
      CONSTRAIN(f0, -0.5f, 0.499999f);
    }
    const float absolute_f0 = max(fabsf(f0), 1.0e-7f);
    const float gain =
        (1.0f / (absolute_f0 * 131072.0f)) * (0.95f - absolute_f0);
    const float cutoff = min(kTableSizeF * absolute_f0, 1.0f);
    
    ONE_POLE(x_lp_, x_modulation.Next(), lp_coefficient);
    ONE_POLE(y_lp_, y_modulation.Next(), lp_coefficient);
    ONE_POLE(z_lp_, z_modulation.Next(), lp_coefficient);
    
    const float x = x_lp_;
    const float y = y_lp_;
    const float z = z_lp_;

    MAKE_INTEGRAL_FRACTIONAL(x);
    MAKE_INTEGRAL_FRACTIONAL(y);
    MAKE_INTEGRAL_FRACTIONAL(z);

    if (process_hard_sync) {
      if (hard_sync & 1) {
        // Reset only the table traversal. Coordinate smoothing,
        // differentiation, and stereo phase rotation remain continuous.
        phase_ = 0.0f;
      }
      hard_sync >>= 1;
    }
    phase_ += f0;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    } else if (phase_ < 0.0f) {
      phase_ += 1.0f;
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
