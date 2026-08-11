// Copyright 2026 Dylan Bolink.
// SPDX-License-Identifier: MIT

#include "plaits/dsp/engine2/clap_engine.h"

#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

#include "plaits/build_config.h"

namespace plaits {

using namespace stmlib;

const float kClapMakeup = 3.4f;
// Reference product of geometric centre and bandwidth: C4 with TONE at noon.
// 0.0218 * 1.2806 = 0.02792 for the centre, times 2^0.6432 - 2^-0.6432 = 0.9214.
const float kClapBandReference = 0.025726f;
const float kClapTailTau = 32.0e-3f;

const float kClapTailSemitonesDown = 22.3f;
const float kClapTailSemitonesUp = 64.4f;

const float kClapTailPan = 0.707106781f;

void ClapEngine::Init(BufferAllocator* allocator) {
  for (int i = 0; i < 2; ++i) {
    highpass_[i].Init();
    highpass_aux_[i].Init();
  }
  lowpass_.Init();
  lowpass_aux_.Init();
  Reset();
}

void ClapEngine::Reset() {
  for (int i = 0; i < 2; ++i) {
    highpass_[i].Reset();
    highpass_aux_[i].Reset();
  }
  lowpass_.Reset();
  lowpass_aux_.Reset();

  countdown_ = kClapIdle;
  slaps_remaining_ = 0;
  slap_index_ = 0;
  slap_env_ = 0.0f;
  tail_env_ = 0.0f;
  slap_gain_l_ = kClapTailPan;
  slap_gain_r_ = kClapTailPan;

  spacing_samples_ = static_cast<int>(10.0e-3f * kSampleRate);
  jitter_ = 0.0f;
  pan_spread_ = 0.0f;
  slap_level_ = 0.0f;
  last_slap_gain_ = 1.0f;
  num_slaps_ = kClapStockSlaps;
  free_run_ = false;
  burst_gain_ = 1.0f;
  free_run_gap_ = static_cast<int>(0.35f * kSampleRate);
  slap_decay_ = 0.0f;
  tail_decay_ = 0.0f;
  prev_makeup_ = 0.0f;
  prev_makeup_aux_ = 0.0f;
}

void ClapEngine::FireSlap() {
  float amplitude = slap_level_ * burst_gain_;
  burst_gain_ *= kClapHandDecline;
  float position = 0.5f;

  if (slap_index_ > 0 || free_run_) {
    const float level_draw = Random::GetFloat();
    const float position_draw = Random::GetFloat();
    amplitude *= 1.0f - jitter_ * level_draw;
    position += (position_draw - 0.5f) * pan_spread_;
  }

  if (slap_index_ == num_slaps_ - 1) {
    amplitude *= last_slap_gain_;
  }

  slap_env_ = amplitude;
  StereoPanGains(position, &slap_gain_l_, &slap_gain_r_);

  ++slap_index_;
  --slaps_remaining_;

  if (slaps_remaining_ > 0) {
    // The gap before the final hand runs short. See kClapLastGapRatio.
    const float base = static_cast<float>(spacing_samples_) *
        (slaps_remaining_ == 1 ? kClapLastGapRatio : 1.0f);
    const float interval_draw = Random::GetFloat();
    const float interval = base * (1.0f + jitter_ * (interval_draw - 0.5f));
    countdown_ = static_cast<int>(interval);
    if (countdown_ < 1) {
      countdown_ = 1;
    }
    return;
  }

  // The room opens under the last hand.
  FireTail();

  if (!free_run_) {
    countdown_ = kClapIdle;
    return;
  }

  const float gap_draw = Random::GetFloat();
  const float gap = static_cast<float>(free_run_gap_) *
      (0.2f + 1.8f * gap_draw * gap_draw);
  countdown_ = static_cast<int>(gap);
  if (countdown_ < 1) {
    countdown_ = 1;
  }
  slaps_remaining_ = 1;
  slap_index_ = 0;
}

void ClapEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const bool sustain = parameters.trigger & TRIGGER_UNPATCHED;
  const bool trigger = parameters.trigger & TRIGGER_RISING_EDGE;

  // HARMONICS
  const float hands = ApplyMacro(static_cast<float>(kClapStockSlaps), 1.0f,
      static_cast<float>(kClapMaxSlaps), parameters.harmonics);
  int slaps = static_cast<int>(hands);
  float last_gain = hands - static_cast<float>(slaps);
  if (last_gain > 0.0f) {
    ++slaps;
  } else {
    last_gain = 1.0f;
  }
  CONSTRAIN(slaps, 1, kClapMaxSlaps);

  float jitter = (parameters.harmonics - 0.5f) * 2.0f;
  CONSTRAIN(jitter, 0.0f, 1.0f);

  num_slaps_ = slaps;
  last_slap_gain_ = last_gain;
  jitter_ = jitter * 0.45f;
  pan_spread_ = 0.2f + 0.8f * parameters.harmonics;
  slap_level_ = 0.3f + 0.7f * parameters.accent;
  free_run_ = sustain;

  // MACRO
  const float spacing_ms = ApplyMacro(10.0f, 4.0f, 35.0f, parameters.macro);
  spacing_samples_ = static_cast<int>(spacing_ms * 1.0e-3f * kSampleRate);

  // MORPH
  const float slap_tau = (2.2f + 1.4f * parameters.morph) * 1.0e-3f * kSampleRate;
  slap_decay_ = 1.0f - 1.0f / slap_tau;
  const float tail_semitones = (2.0f * parameters.morph - 1.0f) *
      (parameters.morph < 0.5f ? kClapTailSemitonesDown : kClapTailSemitonesUp);
  const float tail_tau = kClapTailTau * kSampleRate *
      SemitonesToRatio(tail_semitones);
  tail_decay_ = 1.0f - 1.0f / tail_tau;

  if (sustain) {
    // Free-running HARMONICS stops being a count and becomes a density.
    num_slaps_ = 1;
    last_slap_gain_ = 1.0f;
    if (jitter_ < 0.5f) {
      jitter_ = 0.5f;
    }
    free_run_gap_ = static_cast<int>(0.35f * kSampleRate *
        SemitonesToRatio(parameters.harmonics * -58.0f));
  }

  if (trigger) {
    ArmBurst(num_slaps_);
  } else if (free_run_ && countdown_ == kClapIdle) {
    ArmBurst(1);
  }

  // FREQUENCY places the band, TONE sets how wide it is. The two corners move
  // apart around a fixed geometric centre, so widening does not also shift the
  // pitch of the band.
  const float half_width = ApplyMacro(kClapHalfWidthStock, kClapHalfWidthMin,
      kClapHalfWidthMax, parameters.timbre);
  const float spread = SemitonesToRatio(half_width * 12.0f);
  const float inv_spread = 1.0f / spread;

  const float center = NoteToFrequency(parameters.note) * 4.0f *
      kClapBandCenterRatio;

  float highpass = center * inv_spread;
  CONSTRAIN(highpass, 0.001f, 0.40f);
  highpass_[0].set_f_q<FREQUENCY_FAST>(highpass, kClapHighpassQ1);
  highpass_[1].set_f_q<FREQUENCY_FAST>(highpass, kClapHighpassQ2);
  float lowpass = center * spread;
  CONSTRAIN(lowpass, 0.002f, 0.45f);
  lowpass_.set_f_q<FREQUENCY_FAST>(lowpass, kClapLowpassQ);

  // Noise power goes as the band's width in Hz, so the trim now has to follow
  // TONE as well as the note.
  const float bandwidth = spread - inv_spread;
  const float makeup = kClapMakeup *
      Sqrt(kClapBandReference / (center * bandwidth));

#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  if (parameters.frequency_offset) {
    const float base_root_frequency = NoteToFrequency(parameters.note);
    float last_makeup = makeup;
    float last_makeup_aux = makeup;
    if (PLAITS_STEREO_CLAP && parameters.stereo) {
      for (size_t i = 0; i < size; ++i) {
        float root_frequency = base_root_frequency +
            parameters.frequency_offset[i];
        if (root_frequency < 1.0e-7f) {
          root_frequency = 1.0e-7f;
        }
        const float sample_center =
            root_frequency * 4.0f * kClapBandCenterRatio;
        float sample_highpass = sample_center * inv_spread;
        CONSTRAIN(sample_highpass, 0.001f, 0.40f);
        highpass_[0].set_f_q<FREQUENCY_FAST>(
            sample_highpass, kClapHighpassQ1);
        highpass_[1].set_f_q<FREQUENCY_FAST>(
            sample_highpass, kClapHighpassQ2);
        highpass_aux_[0].set(highpass_[0]);
        highpass_aux_[1].set(highpass_[1]);
        float sample_lowpass = sample_center * spread;
        CONSTRAIN(sample_lowpass, 0.002f, 0.45f);
        lowpass_.set_f_q<FREQUENCY_FAST>(sample_lowpass, kClapLowpassQ);
        lowpass_aux_.set_f_q<FREQUENCY_FAST>(
            sample_lowpass, kClapLowpassQ);
        last_makeup = kClapMakeup *
            Sqrt(kClapBandReference / (sample_center * bandwidth));

        Tick();
        const float noise_left = 2.0f * Random::GetFloat() - 1.0f;
        const float noise_right = 2.0f * Random::GetFloat() - 1.0f;
        const float room = tail_env_ * kClapTailPan;
        out[i] = SoftClip(
            Band(noise_left * (slap_env_ * slap_gain_l_ + room)) *
            last_makeup);
        aux[i] = SoftClip(
            BandAux(noise_right * (slap_env_ * slap_gain_r_ + room)) *
            last_makeup);
      }
      prev_makeup_ = last_makeup;
      prev_makeup_aux_ = last_makeup;
      return;
    }

    const float bandwidth_aux =
        kClapLowpassOctavesAux * spread - inv_spread;
    for (size_t i = 0; i < size; ++i) {
      float root_frequency = base_root_frequency +
          parameters.frequency_offset[i];
      if (root_frequency < 1.0e-7f) {
        root_frequency = 1.0e-7f;
      }
      const float sample_center =
          root_frequency * 4.0f * kClapBandCenterRatio;
      float sample_highpass = sample_center * inv_spread;
      CONSTRAIN(sample_highpass, 0.001f, 0.40f);
      highpass_[0].set_f_q<FREQUENCY_FAST>(
          sample_highpass, kClapHighpassQ1);
      highpass_[1].set_f_q<FREQUENCY_FAST>(
          sample_highpass, kClapHighpassQ2);
      highpass_aux_[0].set(highpass_[0]);
      highpass_aux_[1].set(highpass_[1]);
      float sample_lowpass = sample_center * spread;
      CONSTRAIN(sample_lowpass, 0.002f, 0.45f);
      lowpass_.set_f_q<FREQUENCY_FAST>(sample_lowpass, kClapLowpassQ);
      float sample_lowpass_aux =
          sample_lowpass * kClapLowpassOctavesAux;
      CONSTRAIN(sample_lowpass_aux, 0.002f, 0.45f);
      lowpass_aux_.set_f_q<FREQUENCY_FAST>(
          sample_lowpass_aux, kClapLowpassQ);
      last_makeup = kClapMakeup *
          Sqrt(kClapBandReference / (sample_center * bandwidth));
      last_makeup_aux = kClapMakeup *
          Sqrt(kClapBandReference / (sample_center * bandwidth_aux));

      Tick();
      const float noise = 2.0f * Random::GetFloat() - 1.0f;
      const float hands_only = noise * slap_env_;
      out[i] = SoftClip(
          Band(hands_only + noise * tail_env_) * last_makeup);
      aux[i] = SoftClip(BandAux(hands_only) * last_makeup_aux);
    }
    prev_makeup_ = last_makeup;
    prev_makeup_aux_ = last_makeup_aux;
    return;
  }
#endif

  if (PLAITS_STEREO_CLAP && parameters.stereo) {
    highpass_aux_[0].set(highpass_[0]);
    highpass_aux_[1].set(highpass_[1]);
    lowpass_aux_.set_f_q<FREQUENCY_FAST>(lowpass, kClapLowpassQ);
    ParameterInterpolator gain(&prev_makeup_, makeup, size);
    ParameterInterpolator gain_aux(&prev_makeup_aux_, makeup, size);
    for (size_t i = 0; i < size; ++i) {
      Tick();
      const float noise_left = 2.0f * Random::GetFloat() - 1.0f;
      const float noise_right = 2.0f * Random::GetFloat() - 1.0f;
      const float room = tail_env_ * kClapTailPan;
      out[i] = SoftClip(Band(noise_left * (slap_env_ * slap_gain_l_ + room)) *
          gain.Next());
      aux[i] = SoftClip(BandAux(noise_right * (slap_env_ * slap_gain_r_ + room)) *
          gain_aux.Next());
    }
    return;
  }

  highpass_aux_[0].set(highpass_[0]);
  highpass_aux_[1].set(highpass_[1]);
  float lowpass_aux = lowpass * kClapLowpassOctavesAux;
  CONSTRAIN(lowpass_aux, 0.002f, 0.45f);
  lowpass_aux_.set_f_q<FREQUENCY_FAST>(lowpass_aux, kClapLowpassQ);

  // Derived from AUX's own wider band rather than trimmed by hand, so the two
  // outputs stay matched as TONE moves.
  const float bandwidth_aux = kClapLowpassOctavesAux * spread - inv_spread;
  const float makeup_aux = kClapMakeup *
      Sqrt(kClapBandReference / (center * bandwidth_aux));

  ParameterInterpolator gain(&prev_makeup_, makeup, size);
  ParameterInterpolator gain_aux(&prev_makeup_aux_, makeup_aux, size);

  for (size_t i = 0; i < size; ++i) {
    Tick();
    const float noise = 2.0f * Random::GetFloat() - 1.0f;
    const float hands_only = noise * slap_env_;
    out[i] = SoftClip(Band(hands_only + noise * tail_env_) * gain.Next());
    aux[i] = SoftClip(BandAux(hands_only) * gain_aux.Next());
  }
}

}  // namespace plaits
