// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' PLUK: three round-robin Karplus-Strong voices.

#include "plaits/dsp/engine2/plucked_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/build_config.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

const float kPluckedPan[kNumPluckVoices] = { 0.15f, 0.5f, 0.85f };

namespace {

// The struck period for one voice, clamped to what the fixed-size delay
// line can hold (SPEC R6: reciprocal of NoteToFrequency, never multiplied
// by kCorrectedSampleRate).
inline float PluckedPeriod(float note) {
  float period = 1.0f / NoteToFrequency(note);
  CONSTRAIN(period, kPluckedMinPeriod, kPluckedDelaySize - 4.0f);
  return period;
}

// Mirrors the shift search at digital_oscillator.cc:1099-1105: the smallest
// power-of-two buffer Braids would pick for this pitch, which is >= the true
// period but not equal to it (a coarse, power-of-two-only quantisation).
// Used ONLY to scale COLOR's excitation-length fraction the way Braids
// scales it against its own `size`, not against the true period the way the
// delay-line reads (PluckedPeriod) do -- using the true period there instead
// measured 10-15% short at COLOR extremes and read audibly duller/quieter
// through the harmonic-rich mid band than Braids in every case, corrected by
// this.
inline float PluckedQuantizedSize(float note) {
  float doubled_increment = NoteToFrequency(note) * 4294967296.0f;
  float size = 1024.0f;
  while (doubled_increment > 8388608.0f) {
    doubled_increment *= 0.5f;
    size *= 0.5f;
  }
  CONSTRAIN(size, 8.0f, static_cast<float>(kPluckedDelaySize));
  return size;
}

}  // namespace

void PluckedEngine::Init(BufferAllocator* allocator) {
  for (int v = 0; v < kNumPluckVoices; ++v) {
    delay_line_[v].Init(allocator->Allocate<float>(kPluckedDelaySize));
  }
  Reset();
}

void PluckedEngine::Reset() {
  for (int v = 0; v < kNumPluckVoices; ++v) {
    delay_line_[v].Reset();
    period_[v] = kPluckedDelaySize * 0.25f;
    min_period_[v] = kPluckedMinPeriod;
    excitation_remaining_[v] = 0.0f;
  }
  active_voice_ = 0;
  active_offset_semitones_ = 0.0f;
  ever_struck_ = false;
  loss_frac_ = 0.0f;
  update_probability_frac_ = 1.0f;
}

void PluckedEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  const bool stereo = PLAITS_STEREO_PLUCKED && parameters.stereo;

  // TIMBRE: Damping, two regimes split at TIMBRE 0.5
  // (digital_oscillator.cc:1125-1136). The pitch term reads the CURRENT
  // played note, unspread -- Braids' `loss` is computed from the globally
  // played pitch, not each voice's own struck pitch (header RATE note).
  const float frequency = NoteToFrequency(parameters.note);
  float loss_pitch_term = kPluckedLossPitchBase -
      frequency * kPluckedLossFreqScale;
  CONSTRAIN(loss_pitch_term, kPluckedLossPitchFloor, kPluckedLossPitchBase);

  const float p0 = parameters.timbre * kPluckedParamFull;
  float target_update_probability;
  float target_loss_raw;
  if (p0 < kPluckedTimbreHalf) {
    target_update_probability = kPluckedUpdateProbabilityFull;
    target_loss_raw = loss_pitch_term *
        (kPluckedTimbreHalf - p0) / kPluckedTimbreHalf;
  } else {
    target_update_probability = kPluckedUpdateProbabilityBase -
        p0 * kPluckedUpdateProbabilitySlope;
    target_loss_raw = 0.0f;
  }
  float target_loss_frac = target_loss_raw / kPluckedLossFull;
  float target_update_frac = target_update_probability /
      kPluckedProbabilityFull;
  CONSTRAIN(target_update_frac, 0.0f, 1.0f);

  // MACRO: Stretch. Scales the shortfall of the update fraction below 1.0,
  // i.e. how far TIMBRE's top half is allowed to push the stochastic
  // detuning. 1.0 at macro=0.5 reproduces Braids exactly.
  const float stretch_scale = ApplyMacro(
      1.0f, 0.0f, kPluckedMaxStretch, parameters.macro);
  target_update_frac = 1.0f - (1.0f - target_update_frac) * stretch_scale;
  CONSTRAIN(target_update_frac, 0.0f, 1.0f);

  ParameterInterpolator loss_modulation(&loss_frac_, target_loss_frac, size);
  ParameterInterpolator update_modulation(
      &update_probability_frac_, target_update_frac, size);

  // MORPH: Spread. Zero at noon reproduces the module (and inharmonic-
  // string) exactly -- every round-robin voice unison with the played note.
  const float spread = (parameters.morph - 0.5f) * 2.0f * kPluckedMaxSpread;

  // TRIGGER_UNPATCHED has no Braids equivalent (the module always strikes
  // from its own trigger). Without this the engine renders pure SILENCE
  // with nothing patched to TRIG -- every sample of output here comes from
  // a strike. Follows the sibling stock engines' "unpatched fires once"
  // convention (struck_bell_engine.cc:65-70, struck_drum_engine.cc:84-89,
  // snare_engine.cc:171-177).
  const bool unpatched = (parameters.trigger & TRIGGER_UNPATCHED) != 0;
  const bool rising_edge = (parameters.trigger & TRIGGER_RISING_EDGE) != 0;
  const bool self_strike = unpatched && !ever_struck_;

  if (rising_edge || self_strike) {
    active_voice_ = (active_voice_ + 1) % kNumPluckVoices;
    active_offset_semitones_ = spread * static_cast<float>(active_voice_);

    const float struck_period = PluckedPeriod(
        parameters.note + active_offset_semitones_);
    period_[active_voice_] = struck_period;
    min_period_[active_voice_] = struck_period * 0.5f;

    // COLOR: Pluck, read once at strike time
    // (digital_oscillator.cc:1110-1112, 1114).
    const float p1 = parameters.harmonics * kPluckedParamFull;
    const float width = kPluckedWidthGain * p1;
    float excitation_fraction = (kPluckedWidthBase + width) /
        kPluckedWidthFull;
    CONSTRAIN(excitation_fraction, 0.0f, 1.0f);
    const float quantized_size = PluckedQuantizedSize(
        parameters.note + active_offset_semitones_);
    excitation_remaining_[active_voice_] = max(
        1.0f, excitation_fraction * quantized_size);
    ever_struck_ = true;
  }

  // Continuous pitch tracking for the currently active voice only
  // (digital_oscillator.cc:1120-1122): follows the played note upward by
  // at most an octave above where it was struck, unclamped downward. The
  // other two voices stay frozen at whatever pitch they were struck with.
  {
    float tracked = PluckedPeriod(
        parameters.note + active_offset_semitones_);
    tracked = max(tracked, min_period_[active_voice_]);
    period_[active_voice_] = tracked;
  }

  if (stereo) {
    fill(&out[0], &out[size], 0.0f);
    fill(&aux[0], &aux[size], 0.0f);
  }

  for (size_t i = 0; i < size; ++i) {
    const float loss_frac = loss_modulation.Next();
    const float update_frac = update_modulation.Next();
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      float instantaneous_frequency =
          frequency + parameters.frequency_offset[i];
      instantaneous_frequency *=
          SemitonesToRatio(active_offset_semitones_);
      instantaneous_frequency = max(instantaneous_frequency, 1e-7f);
      float tracked_period = 1.0f / instantaneous_frequency;
      CONSTRAIN(
          tracked_period, kPluckedMinPeriod, kPluckedDelaySize - 4.0f);
      period_[active_voice_] = max(
          tracked_period, min_period_[active_voice_]);
    }
#endif

    float voice_sample[kNumPluckVoices];
    float sum_out = 0.0f;
    float sum_aux = 0.0f;

    for (int v = 0; v < kNumPluckVoices; ++v) {
      float sample;
      if (excitation_remaining_[v] > 0.0f) {
        // digital_oscillator.cc:1149-1151: 25% of the content about to be
        // evicted blended with 75% fresh noise -- on a fresh voice the old
        // content is silence, so this is 0.75x full-scale noise, not 1.0x.
        const float noise = static_cast<float>(Random::GetSample()) *
            (1.0f / 32768.0f);
        const float old = delay_line_[v].Read(period_[v]);
        const float excited = 0.25f * old + 0.75f * noise;
        delay_line_[v].Write(excited);
        sample = excited;
        excitation_remaining_[v] -= 1.0f;
        sum_aux += excited;
      } else {
        const float period = period_[v];
        const float a = delay_line_[v].ReadHermite(period);
        const float b = delay_line_[v].ReadHermite(period - 1.0f);
        const float filtered = 0.5f * (a + b) * (1.0f - loss_frac);
        const float gate = Random::GetFloat();
        const float value = (gate < update_frac) ? filtered : a;
        delay_line_[v].Write(value);
        sample = value;
      }
      voice_sample[v] = sample;
      sum_out += sample;
    }

    if (stereo) {
      for (int v = 0; v < kNumPluckVoices; ++v) {
        float left_gain, right_gain;
        StereoPanGains(kPluckedPan[v], &left_gain, &right_gain);
        out[i] += voice_sample[v] * left_gain;
        aux[i] += voice_sample[v] * right_gain;
      }
      CONSTRAIN(out[i], -1.0f, 1.0f);
      CONSTRAIN(aux[i], -1.0f, 1.0f);
    } else {
      CONSTRAIN(sum_out, -1.0f, 1.0f);
      CONSTRAIN(sum_aux, -1.0f, 1.0f);
      out[i] = sum_out;
      aux[i] = sum_aux;
    }
  }
}

}  // namespace plaits
