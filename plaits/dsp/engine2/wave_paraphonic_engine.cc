// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' WAVE PARAPHONIC: four wavetable voices tuned by Plaits' shared
// chord table.

#include "plaits/dsp/engine2/wave_paraphonic_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/build_config.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/utils/random.h"

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// ChordBank stores frequency ratios while Spread is defined in pitch space.
// The bare-metal firmware cannot afford libm log2f (it pulls in __errno), so
// use the same branch-free approximation Helix uses for chord-table ratios.
inline float FastLog2(float x) {
  union { float f; uint32_t i; } u = { x };
  const float e = static_cast<float>((u.i >> 23) & 0xFFu) - 127.0f;
  u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
  const float m = u.f;
  return e + (-1.7417939f + (2.8212026f + (-1.4699568f
             + (0.44717955f - 0.056570851f * m) * m) * m) * m);
}

// The scan line. Braids' mini_wave_line (digital_oscillator.cc:1622-1626) names
// 33 waves in its own wt_waves; this is the corresponding wave in Plaits'
// wav_integrated_waves, which is generated from the SAME waves.bin (verified
// byte-identical to wt_waves, all 33,024 bytes).
//
// 16 slots have an exact counterpart in Plaits' bank 3 -- slots 4, 5, 7, 10,
// 11, 12, 14, 15, 17, 19, 21, 22, 23, 24, 25 and 32 -- and A/B against the
// module at 0.05 to 0.29 dB with the scan parked on the slot. The other 17
// take the nearest wave in the 192-wave bank, shortlisted offline and then
// chosen by measuring each candidate through the A/B; they land at 0.57 to
// 2.24 dB, median 1.12, the worst being slot 31 (Braids wave 135). Both tables
// were derived from the two resource files by script, never retyped. The
// header has the full method, the per-slot numbers, and the caveat that 15 of
// the 16 "exact" waves are phase-flattened by wavetables.py.
const uint8_t kWaveIndex[kWaveParaphonicNumSlots] = {
  182,  41,  57, 182, 145, 146, 122, 147,  57, 117, 151,
  167, 166, 165, 163, 162,  96, 129,  90, 130, 170, 132,
  134, 136, 138, 141, 140,  60,  62, 173, 121, 147, 178
};

// wavetables.py peak-normalises every wave it emits; Braids' 8-bit waves are
// not normalised, so each slot needs its Braids RMS restored. Stored in 1/128
// steps, spanning 0.734 to 1.703, quantised to within 0.042 dB.
const uint8_t kWaveGain[kWaveParaphonicNumSlots] = {
  121, 121, 130, 166, 118, 120, 100, 125, 147, 137, 218,
  121, 111, 127, 124, 121, 104, 103,  98, 104, 122, 141,
  126, 108, 112, 119, 106, 122, 125, 120,  94, 151, 127
};

// wav_integrated_waves is 192 waves of 128 samples plus a 4-sample guard
// (plaits/dsp/engine/wavetable_engine.cc:88, WAV_INTEGRATED_WAVES_SIZE 25344).
const int kWaveTableSize = 128;
const int kWaveStride = kWaveTableSize + 4;

// wavetables.py stores the running sum scaled by 4 * 32768 / 128, so the
// difference of two adjacent entries IS the peak-normalised wave sample times
// 1024. The guard region makes that difference defined at the wrap.
const float kIntegratedScale = 1.0f / 1024.0f;

inline const int16_t* WaveAt(int slot) {
  return wav_integrated_waves + size_t(kWaveIndex[slot]) * kWaveStride;
}

// One voice's readout position, the crossfade, and the PER-WAVE gain each of
// the two slots needs. Two gains, not one: Braids crossfades the two waves at
// the levels its own 8-bit data has, so the level restoration belongs on each
// wave BEFORE the crossfade. Folding it into a single interpolated gain
// applied to the mixed result is a different expression -- it gives both waves
// the average of their two gains -- and it is wrong wherever the neighbours'
// gains differ. Measured, at the midpoint of the widest step on the line
// (slot 10, gain 1.703, against slot 11 at 0.945, both EXACT wave matches so
// nothing else can be blamed): the single-gain form read AC RMS +1.98 dB and
// spectrum 1.40 dB against the module, where the two slot centres either side
// read +0.19 / -0.10 dB and 0.41 / 0.25 dB. See tests/ab.json `wave-gain-step`.
struct WaveTap {
  const int16_t* low;
  const int16_t* high;
  float crossfade;
  float gain_low;
  float gain_high;
};

// Braids' :1794-1796, with the fan offset added before the split. `ticks` is
// parameter_[0]'s 15-bit domain, so the step arithmetic and the 16-bit
// crossfade fraction are the module's own.
inline void ResolveTap(int ticks, WaveTap* tap) {
  CONSTRAIN(ticks, 0, 32767);
  const int slot = ticks / kWaveParaphonicStep;
  const float crossfade = float(ticks - slot * kWaveParaphonicStep) /
      float(kWaveParaphonicStep);
  tap->low = WaveAt(slot);
  tap->high = WaveAt(slot + 1);
  tap->crossfade = crossfade;
  tap->gain_low = float(kWaveGain[slot]) / 128.0f;
  tap->gain_high = float(kWaveGain[slot + 1]) / 128.0f;
}

// One wave sample, LINEARLY interpolated -- Braids' Interpolate824 readout
// (stmlib/utils/dsp.h:106-111), reproduced against a table recovered by
// differencing the integral.
//
// Differentiating LAST rather than first is the point. Plaits' own readout
// interpolates the INTEGRAL and differences the result, which -- because a
// linearly interpolated integral has constant slope inside a table cell --
// reconstructs the wave as a zero-order HOLD. Its images fall off as
// sinc(f/f_table) where Braids' linear interpolation falls off as sinc^2, and
// the first version of this port measured exactly that: +16.3 dB at 10-20 kHz
// and +22.1 dB above 20 kHz against the reference, on a case whose two waves
// are exact matches. Plaits hides those steps behind the
// cutoff = min(128 * f0, 1) one-poles; Braids has no such filter, so the port
// removes the steps at the source instead of filtering them afterwards.
inline float ReadWave(const WaveTap& tap, float phase) {
  const float p = phase * float(kWaveTableSize);
  MAKE_INTEGRAL_FRACTIONAL(p);
  const float low_0 = float(tap.low[p_integral + 1] - tap.low[p_integral]);
  const float low_1 = float(tap.low[p_integral + 2] - tap.low[p_integral + 1]);
  const float high_0 = float(tap.high[p_integral + 1] - tap.high[p_integral]);
  const float high_1 =
      float(tap.high[p_integral + 2] - tap.high[p_integral + 1]);
  const float a = (low_0 + (low_1 - low_0) * p_fractional) * tap.gain_low;
  const float b = (high_0 + (high_1 - high_0) * p_fractional) * tap.gain_high;
  return (a + (b - a) * tap.crossfade) * kIntegratedScale;
}

}  // namespace

void WaveParaphonicEngine::Init(BufferAllocator* allocator) {
  chords_.Init(allocator);
  Reset();
}

void WaveParaphonicEngine::Reset() {
  chords_.Reset();
  for (int i = 0; i < kWaveParaphonicNumVoices; ++i) {
    phase_[i] = 0.0f;
    frequency_[i] = 0.01f;
  }
  // Braids sets strike_ and consumes it inside Render (digital_oscillator.cc:
  // 1759-1764), so the Random draws happen once even though Init() and Reset()
  // may both run before the first block.
  strike_ = true;
}

void WaveParaphonicEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    strike_ = true;
  }

  const bool stereo = PLAITS_STEREO_WAVE_PARAPHONIC && parameters.stereo;

  // HARMONICS selects one position in the active firmware chord table. This
  // is the compatibility contract shared with the other chord-bank engines.
  // Wave Paraphonic always sounds all four stored voices; `arpLength` remains
  // an arpeggiator concern and does not mute oscillators here.
  chords_.set_chord(parameters.harmonics, parameters.chord_set_option);

  // Braids' TIMBRE, :1794-1796, plus the port's per-voice fan.
  const int timbre = static_cast<int>(parameters.timbre * 32767.0f);
  const int timbre_ticks = timbre < 0 ? 0 : (timbre > 32767 ? 32767 : timbre);
  const int fan = static_cast<int>((parameters.morph - 0.5f) * 2.0f *
      kWaveParaphonicMaxFan * 32767.0f);

  const float spread = ApplyMacro(
      1.0f,
      kWaveParaphonicMinSpread,
      kWaveParaphonicMaxSpread,
      parameters.macro);

  WaveTap tap[kWaveParaphonicNumVoices];
  float target_frequency[kWaveParaphonicNumVoices];
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  float frequency_offset_ratio[kWaveParaphonicNumVoices];
  const float root_frequency = max(
      1e-7f, NoteToFrequency(parameters.note));
#endif
  float pan_left[kWaveParaphonicNumVoices];
  float pan_right[kWaveParaphonicNumVoices];

  if (stereo) {
    // Fixed positions, so these are per-block, never per-sample.
    for (int i = 0; i < kWaveParaphonicNumVoices; ++i) {
      const float angle = kWaveParaphonicPan[i] * 1.5707963f;
      pan_left[i] = cosf(angle);
      pan_right[i] = sinf(angle);
    }
  }

  for (int i = 0; i < kWaveParaphonicNumVoices; ++i) {
    ResolveTap(timbre_ticks + i * fan, &tap[i]);

    // ChordBank exposes ratios; return to log-pitch space so Spread remains a
    // musical 0x..2x interval scaler, including for rootless/inverted tables.
    const float interval = 12.0f * FastLog2(chords_.ratio(i));
    const float note = parameters.note + interval * spread;
    target_frequency[i] = NoteToFrequency(note);
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    frequency_offset_ratio[i] = target_frequency[i] / root_frequency;
#endif
  }

  // :1759-1764. Four words, drawn here rather than in Reset() so the count
  // never depends on how often Init()/Reset() ran before the first block.
  if (strike_) {
    for (int i = 0; i < kWaveParaphonicNumVoices; ++i) {
      phase_[i] = static_cast<float>(Random::GetWord()) / 4294967296.0f;
      frequency_[i] = target_frequency[i];
    }
    strike_ = false;
  }

  ParameterInterpolator fm_0(&frequency_[0], target_frequency[0], size);
  ParameterInterpolator fm_1(&frequency_[1], target_frequency[1], size);
  ParameterInterpolator fm_2(&frequency_[2], target_frequency[2], size);
  ParameterInterpolator fm_3(&frequency_[3], target_frequency[3], size);
  ParameterInterpolator* fm[kWaveParaphonicNumVoices] = {
    &fm_0, &fm_1, &fm_2, &fm_3
  };

#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  size_t frequency_sample = 0;
#endif
  while (size--) {
    float sum = 0.0f;
    float left = 0.0f;
    float right = 0.0f;
    float first_voice = 0.0f;

    for (int i = 0; i < kWaveParaphonicNumVoices; ++i) {
      float increment = fm[i]->Next();
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
      if (parameters.frequency_offset) {
        increment = max(
            0.0f, increment + parameters.frequency_offset[frequency_sample] *
                frequency_offset_ratio[i]);
      }
#endif
      float phase = phase_[i] + increment;
      if (phase >= 1.0f) {
        phase -= 1.0f;
      }
      phase_[i] = phase;

      const float sample = ReadWave(tap[i], phase);

      sum += sample;
      if (i == 0) {
        first_voice = sample;
      }
      if (stereo) {
        left += sample * pan_left[i];
        right += sample * pan_right[i];
      }
    }

    // :1810 / :1822 sum the four voices and shift right by two, so the port's
    // scale is the module's. Every term is bounded -- each table wave peaks at
    // exactly 1.0 before its gain (wavetables.py peak-normalises), and the
    // largest gain is 1.703 -- so the worst case is a bounded 1.703, and
    // CONSTRAIN rather than the limiter is the right output stage (SPEC R1/R2),
    // which keeps the A/B's level figure linear.
    //
    // Note what that bound means, since it is above 1.0: CONSTRAIN is a real
    // clip, not decoration, and it would engage if all four voices reached
    // their peak on the same sample at slot 10 (gain 1.703), the loudest slot.
    // In practice they do not -- the four phases start decorrelated and the
    // chord detunes them, so the worst observed peak is 0.989 over a 30 s note
    // on the near-unison chord row parked at slot 10, the case built to force
    // the alignment. Braids cannot clip here at all (its sum of four int16s
    // shifted right by two always fits), so this is a declared deviation with
    // no measured instance rather than an equivalence.
    float main = stereo ? left * 0.25f : sum * 0.25f;
    float second = stereo ? right * 0.25f : first_voice * 0.5f;
    CONSTRAIN(main, -1.0f, 1.0f);
    CONSTRAIN(second, -1.0f, 1.0f);

    *out++ = main;
    *aux++ = second;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    ++frequency_sample;
#endif
  }
}

}  // namespace plaits
