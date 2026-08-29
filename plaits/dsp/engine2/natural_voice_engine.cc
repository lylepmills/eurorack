// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// See the header for the format description. Control map:
//   HARMONICS  word bank        } these three match the stock Speech
//   MORPH      word within bank  } engines, so the gestures transfer
//   TIMBRE     vocal tract size (internal-clock scaling, pitch-compensated)
//   MACRO      articulation: log-area-ratio scaling, mumbled..hyper
//   AUX        whisper - the same tract driven by noise only
//
// Two more controls arrive from Voice on the unpatched attenuverters when
// TRIG is patched, as they do for stock Speech: FM -> prosody depth,
// MORPH -> playback speed. See set_prosody_amount / set_speed.
//
// No libm: 2^x via stmlib::SemitonesToRatio, sin via the plaits Sine LUT,
// tanh via the baked LUT in natural_voice_data.h, sqrt via stmlib::Sqrt.

#include "plaits/dsp/engine2/natural_voice_engine.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/dsp/engine2/natural_voice_data.h"

namespace plaits {

using namespace stmlib;

// Engine constants and shared tables always come from the baked header.
namespace data = ::natural_voice_data;

// Bank CONTENT comes from the recipe when a build carries one. The firmware
// force-includes the generated config through a make variable, the way it
// already does for Speech (SPEECH_CONFIG), so there is nothing to #include
// here -- only a flag that defaults to off for stock and SDK builds.
#ifndef PLAITS_HAS_CUSTOM_NATURAL_VOICE_BANKS
#define PLAITS_HAS_CUSTOM_NATURAL_VOICE_BANKS 0
#endif
#if PLAITS_HAS_CUSTOM_NATURAL_VOICE_BANKS
namespace bank = natural_voice_recipe;
#else
namespace bank = ::natural_voice_data;
#endif

namespace {

const float kInternalRate = 16000.0f;
const float kRegisterHz = 100.0f;

// One-pole coefficients for the 16 kHz internal rate (1 - e^(-1/(tau*fs))).
// The k and v targets only move at the 40 Hz frame clock and their time
// constants are 8 and 6 ms, so they are smoothed every kSmoothDecimation
// ticks (4 kHz) with the equivalent coefficient 1 - (1 - a)^N. That takes
// 23 one-poles and 10 square roots off three ticks in four.
const int kSmoothDecimation = 4;
const float kSmoothK = 0.03075f;      // 8 ms, applied every 4th tick
const float kSmoothV = 0.04088f;      // 6 ms, applied every 4th tick
const float kSmoothGain = 0.0206f;    // 3 ms, every tick
const float kSmoothF0 = 0.00623f;     // 10 ms, every tick

// The whisper is broadband noise through the same tract, and an aux path:
// order 6 still carries the gross envelope a devoiced output needs, and the
// saved taps go toward the hardware CPU headroom.
const int kWhisperOrder = 6;

// Exaggeration ceiling for articulation; see DecodeFrame.
const float kArticulationCeiling = 4.6f;

// Loss per lattice section above noon, which is how the tract is kept from
// ringing when its constrictions are exaggerated.
//
// Doubling a frame's deviation from the mean necessarily sharpens its
// resonances -- articulation and Q are the same physical thing -- but a real
// vocal tract is lossy and its formants stay above about 40 Hz of bandwidth.
// Scaling the backward path of every section by mu is exactly that loss:
// each unit delay becomes mu * z^-1, so every pole contracts radially by mu.
//
// The same contraction was first attempted as a[i] *= mu^i around an
// order-18 step-up/step-down of the coefficients. That is ill-conditioned
// near the unit circle and backfired -- measured against the real bank it
// RAISED the worst |k| from 0.998 to 0.9995. Done here in the lattice it is
// 18 multiplies and cannot misbehave. mu is exactly 1 at and below noon, so
// the bypass and the whole mumble half are untouched.
const float kPoleDamping = 0.020f;

const float kJitter = 0.006f;
const float kShimmerDb = 0.4f;
const float kFlutterSt = 0.12f;
const float kOutputGain = 0.4f;

// 10^(dB/20) = 2^(dB * log2(10)/20 * 12 / 12)
inline float DbToAmp(float db) {
  if (db < -90.0f) return 0.0f;
  CONSTRAIN(db, -60.0f, 12.0f);
  return SemitonesToRatio(db * 1.9931569f);
}

inline float TanhLut(float x) {
  float sign = x < 0.0f ? -1.0f : 1.0f;
  float ax = x * sign * data::kTanhLutScale;
  int i = static_cast<int>(ax);
  if (i >= data::kTanhLutSize - 1) return sign * 0.9999f;
  float frac = ax - static_cast<float>(i);
  return sign * (data::kTanhLut[i] +
                 (data::kTanhLut[i + 1] - data::kTanhLut[i]) * frac);
}

inline float Gauss() {
  return (Random::GetFloat() + Random::GetFloat() + Random::GetFloat()
          - 1.5f) * 2.0f;
}

}  // namespace

void NaturalVoiceEngine::Init(stmlib::BufferAllocator* allocator) {
  bank_quantizer_.Init(bank::kNumBanks, 0.1f, false);
  words_in_bank_ = bank::kBankFirstWord[1] - bank::kBankFirstWord[0];
  word_quantizer_.Init(words_in_bank_, 0.1f, false);
  Reset();
}

void NaturalVoiceEngine::Reset() {
  for (int i = 0; i < kOrder; ++i) k_target_[i] = k_[i] = 0.0f;
  for (int b = 0; b < kBands; ++b) v_target_[b] = v_[b] = 0.0f;
  for (int i = 0; i <= kOrder; ++i) {
    lattice_[i] = whisper_lattice_[i] = 0.0f;
  }
  gain_target_ = gain_ = 0.0f;
  f0_st_target_ = f0_st_ = 0.0f;
  voiced_ = false;
  DesignBands();
  smooth_countdown_ = 0;
  for (int b = 0; b < kBands; ++b) UpdateBandWeights(b);
  period_phase_ = 0.0f;
  period_samples_ = kInternalRate / kRegisterHz;
  wavelet_pos_ = 64;
  period_amp_ = 0.0f;
  denormal_guard_ = 1e-15f;
  pole_damp_ = 1.0f;
  jitter_mul_ = 1.0f;
  flutter_phase_[0] = 0.37f;
  flutter_phase_[1] = 0.11f;
  flutter_phase_[2] = 0.79f;
  flutter_countdown_ = 0;
  flutter_value_ = 0.0f;
  // A standalone build has no Voice to drive the attenuverters: play the
  // recorded prosody at its natural depth and the recorded speed.
  prosody_amount_ = 1.0f;
  prosody_now_ = 1.0f;
  speed_bipolar_ = 0.0f;
  bank_ = 0;
  word_ = 0;
  playback_frame_ = -1;
  frame_phase_ = 0.0f;
  word_done_ = true;
  last_decoded_frame_ = -1;
  last_decoded_gamma_ = 1.0f;
  clock_phase_ = 0.0f;
  sample_[0] = sample_[1] = 0.0f;
  next_sample_[0] = next_sample_[1] = 0.0f;
}

void NaturalVoiceEngine::DesignBands() {
  // A COMPLEMENTARY crossover: four lowpasses at 500/1k/2k/4k, with the five
  // bands taken as their successive differences (and the top band as
  // x - lp3), so the bands sum to EXACTLY the input.
  //
  // That identity is load-bearing. The excitation below removes the pulse
  // from unvoiced bands by SUBTRACTING their share of it, which can only
  // silence a fricative if the bank reconstructs. The original bank (an RBJ
  // lowpass + three bandpasses + a highpass) summed to 0.38-1.14 instead,
  // leaking the pulse at -8 dB straight into the 2-8 kHz fricative region:
  // every /s/ came out voiced, as a /z/.
  const float cutoffs[kBands - 1] = { 500.0f, 1000.0f, 2000.0f, 4000.0f };
  // Band noise levels, normalized to the mean pulse-train band power and
  // measured offline against the shipped wavelet through this exact bank.
  const float noise_cal[kBands] = { 1.7724f, 1.9106f, 1.3540f, 0.9144f,
                                    0.4861f };
  for (int b = 0; b < kBands; ++b) {
    noise_cal_[b] = noise_cal[b];
  }
  for (int b = 0; b < kBands - 1; ++b) {
    // RBJ lowpass, Q = 1/sqrt(2), via the Sine LUT (init-time only).
    float w = cutoffs[b] / kInternalRate;   // f/fs
    float s = Sine(w);                      // sin(2*pi*f/fs)
    float c = Sine(w + 0.25f);              // cos(2*pi*f/fs)
    float alpha = s / 1.414f;
    float a0 = 1.0f + alpha;
    float b0 = (1.0f - c) * 0.5f / a0;
    pulse_lp_[b].Set(b0, (1.0f - c) / a0, b0, -2.0f * c / a0,
                     (1.0f - alpha) / a0);
    noise_lp_[b].Set(b0, (1.0f - c) / a0, b0, -2.0f * c / a0,
                     (1.0f - alpha) / a0);
  }
}

void NaturalVoiceEngine::UpdateBandWeights(int band) {
  float v = v_[band];
  CONSTRAIN(v, 0.0f, 1.0f);
  wp_comp_[band] = 1.0f - Sqrt(v);
  wn_cal_[band] = Sqrt(1.0f - v) * noise_cal_[band];
}

void NaturalVoiceEngine::DecodeFrame(int frame_index, float gamma) {
  const uint8_t* f = &bank::kBankFrames[frame_index * data::kBytesPerFrame];
  gain_target_ = f[0] == 0 ? 0.0f : DbToAmp(0.5f * f[0] - 96.0f);
  f0_st_target_ = 0.25f * static_cast<int8_t>(f[1]);
  v_target_[0] = (f[2] & 15) * 0.066667f;
  v_target_[1] = (f[2] >> 4) * 0.066667f;
  v_target_[2] = (f[3] & 15) * 0.066667f;
  v_target_[3] = (f[3] >> 4) * 0.066667f;
  v_target_[4] = (f[4] & 15) * 0.066667f;
  voiced_ = (f[4] >> 4) & 1;
  // Raw-hd frames: 18 int8 log-area-ratios follow the voicing bytes.
  //
  // Articulation interpolates each coefficient toward this bank's MEAN tract
  // rather than scaling it toward zero: lar' = mean + gamma * (lar - mean).
  // Scaling toward zero flattens the tract to a uniform tube, which is its
  // own bright, specific shape -- so the knob swept overall brightness much
  // harder than it swept articulation, and read as a tone control. The mean
  // IS the bank's average spectrum, so interpolating toward it holds average
  // brightness still and scales only each frame's deviation from it, which
  // is what articulation actually is. gamma == 1 reproduces the recording
  // exactly, whatever the clamps below.
  const int8_t* lars = reinterpret_cast<const int8_t*>(&f[5]);
  const int8_t* mean = &bank::kBankMeanLar[bank_ * kOrder];
  const float lar_unit = data::kLarMax / 127.0f;
  const float lar_neutral = lar_unit;
  // The lattice's power gain is prod 1/(1 - k^2); compensate the excitation
  // so articulation reshapes the tract without riding the loudness.
  float gain_num = 1.0f;
  float gain_den = 1.0f;
  for (int i = 0; i < kOrder; ++i) {
    float raw = static_cast<float>(lars[i]);
    float recorded = raw * lar_unit;
    float mean_lar = static_cast<float>(mean[i]) * lar_unit;
    float lar = mean_lar + gamma * (recorded - mean_lar);
    // Bound only the EXAGGERATION. A recorded coefficient passes through
    // untouched however extreme it is (real speech reaches |LAR| 7, and
    // clamping that would colour the gamma == 1 bypass), but pushing one
    // further out is what put a whistle at the top of the range: past about
    // |LAR| 5.5 the pole sits close enough to the unit circle to ring.
    float reach = recorded < 0.0f ? -recorded : recorded;
    if (reach < kArticulationCeiling) reach = kArticulationCeiling;
    CONSTRAIN(lar, -reach, reach);
    k_target_[i] = TanhLut(lar * 0.5f);
    float kn = TanhLut(raw * lar_neutral * 0.5f);
    gain_den *= 1.0f - kn * kn;
  }

  for (int i = 0; i < kOrder; ++i) {
    gain_num *= 1.0f - k_target_[i] * k_target_[i];
  }
  float comp = Sqrt(gain_num / (gain_den + 1e-12f));
  CONSTRAIN(comp, 0.06f, 16.0f);
  gain_target_ *= comp;
}

float NaturalVoiceEngine::InternalTick(float f0_phase_inc, float* whisper) {
  // Parameter smoothing, decimated (see kSmoothDecimation). The band
  // crossfade weights are cached here too: they are pure functions of v_,
  // so recomputing their square roots every tick was the single most
  // expensive thing in this loop (10 VSQRT per internal sample).
  if (--smooth_countdown_ <= 0) {
    smooth_countdown_ = kSmoothDecimation;
    for (int i = 0; i < kOrder; ++i) {
      k_[i] += (k_target_[i] - k_[i]) * kSmoothK;
    }
    for (int b = 0; b < kBands; ++b) {
      v_[b] += (v_target_[b] - v_[b]) * kSmoothV;
      UpdateBandWeights(b);
    }
  }
  gain_ += (gain_target_ - gain_) * kSmoothGain;
  f0_st_ += (f0_st_target_ - f0_st_) * kSmoothF0;

  // Klatt flutter: three slow incommensurate sines, held over 16 ticks.
  if (--flutter_countdown_ <= 0) {
    flutter_countdown_ = 16;
    flutter_phase_[0] += 16.0f * 12.7f / kInternalRate;
    flutter_phase_[1] += 16.0f * 7.1f / kInternalRate;
    flutter_phase_[2] += 16.0f * 4.7f / kInternalRate;
    for (int i = 0; i < 3; ++i) {
      if (flutter_phase_[i] >= 1.0f) flutter_phase_[i] -= 1.0f;
    }
    flutter_value_ = (Sine(flutter_phase_[0]) + Sine(flutter_phase_[1]) +
                      Sine(flutter_phase_[2])) * (kFlutterSt / 3.0f);
  }
  float flutter = flutter_value_;

  // Pulse train with per-period jitter/shimmer.
  float pulse = 0.0f;
  if (voiced_ && gain_target_ > 0.0f) {
    period_phase_ += 1.0f;
    if (period_phase_ >= period_samples_) {
      period_phase_ -= period_samples_;
      jitter_mul_ = 1.0f + kJitter * Gauss();
      float inc = f0_phase_inc *
          SemitonesToRatio(f0_st_ * prosody_now_ + flutter) *
          jitter_mul_;
      CONSTRAIN(inc, 30.0f / kInternalRate, 0.3f);
      period_samples_ = 1.0f / inc;
      wavelet_pos_ = 0;
      float shimmer = SemitonesToRatio(kShimmerDb * Gauss() * 1.9931569f);
      period_amp_ = Sqrt(period_samples_) * shimmer;
    }
  } else {
    period_phase_ = period_samples_;
  }
  if (wavelet_pos_ < 64) {
    pulse = data::kWavelet[wavelet_pos_++] * period_amp_;
  }

  // Full-band pulse minus each band's unvoiced fraction, plus band noise.
  float noise = Gauss();
  float excitation = pulse;
  float p_prev = 0.0f;
  float n_prev = 0.0f;
  for (int b = 0; b < kBands - 1; ++b) {
    const float p_lp = pulse_lp_[b].Process(pulse);
    const float n_lp = noise_lp_[b].Process(noise);
    excitation -= wp_comp_[b] * (p_lp - p_prev);
    excitation += wn_cal_[b] * (n_lp - n_prev);
    p_prev = p_lp;
    n_prev = n_lp;
  }
  excitation -= wp_comp_[kBands - 1] * (pulse - p_prev);
  excitation += wn_cal_[kBands - 1] * (noise - n_prev);
  // A Nyquist-rate speck keeps the lattice state out of the denormal range.
  // Between words the excitation is exactly zero, and a sharp tract decays
  // slowly enough to sit there a long time -- denormals trap to support code
  // on this FPU, so that is CPU cost, not merely lost precision.
  excitation *= gain_;
  // AFTER the gain multiply, not before: between words gain_ decays to zero,
  // which multiplied the guard away exactly when it was needed and left the
  // lattice decaying through the denormal range again.
  denormal_guard_ = -denormal_guard_;
  excitation += denormal_guard_;

  // Main lattice (same loop as the stock LPC synth).
  float f = excitation;
  for (int i = kOrder - 1; i >= 0; --i) {
    f -= k_[i] * lattice_[i];
    lattice_[i + 1] = (lattice_[i] + k_[i] * f) * pole_damp_;
  }
  // Clamp BEFORE storing: this value feeds the next sample, so leaving the
  // state unbounded let a sharply resonant tract run away and crackle
  // against the output limiter.
  CONSTRAIN(f, -4.0f, 4.0f);
  lattice_[0] = f;

  // Whisper: the same tract at order 10, noise-only excitation. Uniform
  // noise (one RNG draw, not the three a Gaussian costs) is perceptually
  // identical once it is through the tract, and stays decorrelated from
  // the main path's noise so OUT and AUX can be summed.
  float w = (Random::GetFloat() - 0.5f) * 2.0f * gain_ * 0.7f;
  for (int i = kWhisperOrder - 1; i >= 0; --i) {
    w -= k_[i] * whisper_lattice_[i];
    whisper_lattice_[i + 1] = (whisper_lattice_[i] + k_[i] * w) * pole_damp_;
  }
  CONSTRAIN(w, -4.0f, 4.0f);
  whisper_lattice_[0] = w;

  *whisper = w;
  return f;
}

void NaturalVoiceEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const bool free_running = parameters.trigger & TRIGGER_UNPATCHED;
  *already_enveloped = !free_running;

  // With nothing in TRIG the engine is parked on a single frame, so its
  // stored pitch offset is not a contour at all -- just a fixed detune that
  // jumps around as MORPH scrubs. Scan mode therefore plays flat, which
  // makes the drone track V/Oct exactly. Prosody is a triggered-utterance
  // control; it comes back the moment TRIG is patched.
  prosody_now_ = free_running ? 0.0f : prosody_amount_;

  // TIMBRE: vocal tract via internal-clock scaling, +-5 semitones.
  //
  // The range is a CPU decision as much as a musical one: every internal
  // tick costs the same, so raising the clock raises the load in direct
  // proportion. At +-8 st the top of the range measured 67% in QEMU but
  // blinked the hardware probe red -- over 90% of the audio deadline --
  // with a plain triggered word and no knob moving. +-5 st takes about a
  // sixth off the worst corner and still spans a wide tract.
  const float rate_ratio = SemitonesToRatio((parameters.timbre - 0.5f) *
                                            10.0f);
  const float rate = rate_ratio * (kInternalRate / kCorrectedSampleRate);

  // Utterances are normalized to a 100 Hz register; pitch tracking
  // compensates the tract scaling so FREQUENCY stays true.
  const float frequency = NoteToFrequency(parameters.note);
  const float pitch_shift =
      frequency / (rate_ratio * kRegisterHz / kCorrectedSampleRate);
  float f0_phase_inc = (kRegisterHz / kInternalRate) * pitch_shift;
  CONSTRAIN(f0_phase_inc, 0.0f, 0.35f);

  // MACRO: articulation depth about the bank's mean tract. 0 = every frame
  // collapses onto the mean (one sustained neutral vowel -- a true mumble),
  // 0.5 = as recorded, 1 = deviations doubled.
  const float gamma = parameters.macro * 2.0f;
  pole_damp_ = gamma > 1.0f ? 1.0f - (gamma - 1.0f) * kPoleDamping : 1.0f;

  // Playback speed comes from the unpatched MORPH attenuverter (Voice), not
  // from a knob: +-2 octaves of rate around the recorded speed at centre.
  const float speed = SemitonesToRatio(speed_bipolar_ * 24.0f);

  // HARMONICS selects the bank, MORPH the word inside it -- the stock
  // Speech layout. Bank sizes may differ, so the word quantizer is re-Init'd
  // on a bank change (HysteresisQuantizer2::Init resets its state and
  // Process clamps, so a stale index is impossible).
  const int bank = bank_quantizer_.Process(parameters.harmonics);
  if (bank != bank_) {
    bank_ = bank;
    words_in_bank_ = bank::kBankFirstWord[bank + 1] -
        bank::kBankFirstWord[bank];
    if (words_in_bank_ < 1) words_in_bank_ = 1;
    word_quantizer_.Init(words_in_bank_, 0.1f, false);
  }
  const int word = bank::kBankFirstWord[bank] +
      word_quantizer_.Process(parameters.morph);
  const int word_start = bank::kWordBoundaries[word];

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    word_ = word;
    playback_frame_ = word_start;
    frame_phase_ = 0.0f;
    word_done_ = false;
    DecodeFrame(playback_frame_, gamma);
    last_decoded_frame_ = playback_frame_;
    last_decoded_gamma_ = gamma;
  }

  if (free_running) {
    // Nothing in TRIG: scan mode, matching what LPC Words does with an
    // unpatched trigger. MORPH stops selecting a word and instead scrubs a
    // position across the WHOLE bank, which drones and is playable by hand.
    // Decoding is bounded to real movement -- the 8 ms coefficient smoothing
    // bridges the steps between frames.
    const int bank_first = bank::kWordBoundaries[bank::kBankFirstWord[bank]];
    const int bank_last =
        bank::kWordBoundaries[bank::kBankFirstWord[bank + 1]];
    int span = bank_last - bank_first - 1;
    if (span < 0) span = 0;
    const int frame = bank_first +
        static_cast<int>(parameters.morph * static_cast<float>(span));
    const float dg = gamma - last_decoded_gamma_;
    if (frame != last_decoded_frame_ || dg > 0.005f || dg < -0.005f) {
      DecodeFrame(frame, gamma);
      last_decoded_frame_ = frame;
      last_decoded_gamma_ = gamma;
    }
    word_ = word;
    word_done_ = true;   // scan mode has no timed playback to advance
  }

  // Frame clock advances in wall-clock time (decoupled from the tract
  // scaling): frames per internal sample = rate_hz * speed / (16k * ratio).
  const float frame_inc =
      data::kFrameRateHz * speed / (kInternalRate * rate_ratio);

  for (size_t n = 0; n < size; ++n) {
    clock_phase_ += rate;
    if (clock_phase_ >= 1.0f) {
      clock_phase_ -= 1.0f;
      sample_[0] = next_sample_[0];
      sample_[1] = next_sample_[1];

      if (!word_done_) {
        frame_phase_ += frame_inc;
        while (frame_phase_ >= 1.0f) {
          frame_phase_ -= 1.0f;
          ++playback_frame_;
          if (playback_frame_ >= bank::kWordBoundaries[word_ + 1]) {
            if (free_running) {
              // Nothing in TRIG: loop the word rather than stopping.
              playback_frame_ = bank::kWordBoundaries[word_];
            } else {
              word_done_ = true;
              gain_target_ = 0.0f;
              break;
            }
          }
          DecodeFrame(playback_frame_, gamma);
          last_decoded_frame_ = playback_frame_;
          last_decoded_gamma_ = gamma;
        }
      }

      float whisper = 0.0f;
      next_sample_[0] = InternalTick(f0_phase_inc, &whisper) * kOutputGain;
      next_sample_[1] = whisper * kOutputGain;
    }
    out[n] = sample_[0] + (next_sample_[0] - sample_[0]) * clock_phase_;
    aux[n] = sample_[1] + (next_sample_[1] - sample_[1]) * clock_phase_;
  }

  if (PLAITS_STEREO_NATURAL_VOICE && parameters.stereo) {
    // Same shape both stock Speech engines use: OUT/AUX become L/R by gently
    // panning the two existing paths rather than hard-splitting them. The
    // voice leans slightly left and the whisper slightly right, but BOTH
    // appear in BOTH channels, so this reads as a breathy widened voice and
    // a mono sum does not cancel.
    float voice_l, voice_r, whisper_l, whisper_r;
    StereoPanGains(0.4f, &voice_l, &voice_r);
    StereoPanGains(0.6f, &whisper_l, &whisper_r);
    for (size_t n = 0; n < size; ++n) {
      const float voice = out[n];
      const float whisper_sample = aux[n];
      out[n] = voice * voice_l + whisper_sample * whisper_l;
      aux[n] = voice * voice_r + whisper_sample * whisper_r;
    }
  }
}

}  // namespace plaits
