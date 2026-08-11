// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' BLOW: a blown-bore waveguide with a nonlinear reed and breath noise.

#include "plaits/dsp/engine2/blown_engine.h"

#include <algorithm>

#include "plaits/build_config.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// lut_flute_body_filter, verbatim from braids/resources.cc:870, first 80
// entries. Reproduces at 0 LSB across all 128 from its generator in
// braids/resources/lookup_tables.py:171,
//
//     floor(4096 * min(0.7, 0.4 * 2^((n - 69) / 12)))
//
// Rounding instead of flooring misses by 1 LSB, so the FLOOR is the reading
// that reproduces, and the port reads the stored integer rather than the
// closed form: the truncation is proportionally largest exactly where the
// values are smallest (-1.46% at n = 0, -0.02% at n = 48). Entries 79..127 are
// all 2867 -- the min(0.7, ...) ceiling -- so the index is clamped at 79 here
// and the tail is not stored.
const uint16_t kBodyFilter[80] = {
     30,    32,    34,    36,    38,    40,    43,    45,
     48,    51,    54,    57,    60,    64,    68,    72,
     76,    81,    86,    91,    96,   102,   108,   114,
    121,   129,   136,   144,   153,   162,   172,   182,
    193,   204,   216,   229,   243,   258,   273,   289,
    306,   325,   344,   364,   386,   409,   433,   459,
    487,   516,   546,   579,   613,   650,   688,   729,
    773,   819,   867,   919,   974,  1032,  1093,  1158,
   1227,  1300,  1377,  1459,  1546,  1638,  1735,  1839,
   1948,  2064,  2187,  2317,  2454,  2600,  2755,  2867
};

// 31-tap Kaiser(beta = 6) halfband, 96 -> 48 kHz. Only the centre tap and the
// odd offsets are non-zero (the even ones are 1.9e-17), so one output costs
// eight multiplies. Chosen by measurement against the reference renderer's
// 127-tap Blackman-Harris halfband on the case with the most energy above
// 24 kHz: 0.11 dB of spectral difference, against 0.40 dB for a 19-tap and
// 0.05 dB for a 51-tap.
const float kHalfbandCentre = 0.4999739547f;
const float kHalfband[8] = {
   0.3144409659f,
  -0.0949999614f,
   0.0465914822f,
  -0.0242523503f,
   0.0119896864f,
  -0.0052090058f,
   0.0017678112f,
  -0.0003156056f
};

// stmlib::Random's generator, so the breath noise has the same distribution as
// Braids' (braids reads Random::GetSample(), the top 16 bits of this LCG as an
// int16). The port carries its own state rather than sharing the global one.
inline float NextNoise(uint32_t* state) {
  *state = *state * 1664525u + 1013904223u;
  return static_cast<float>(
      static_cast<int16_t>(*state >> 16)) * (1.0f / 32768.0f);
}

// Braids' body filter index: `(pitch_ - 8192 + (COLOR >> 1)) >> 7`, clamped to
// 0..127 (digital_oscillator.cc:1324-1329) with pitch_ itself already clamped
// to 0..140*128 by Render() (44, 120-124). The shifts are reproduced rather
// than replaced by their arithmetic equivalents, because the >> 1 on COLOR and
// the >> 7 on the sum both floor.
inline float BodyCoefficient(float note, float harmonics) {
  int pitch = static_cast<int>(note * 128.0f);
  CONSTRAIN(pitch, 0, 140 * 128);
  const int color = static_cast<int>(harmonics * 32767.0f);
  int index = (pitch - 8192 + (color >> 1)) >> 7;
  CONSTRAIN(index, 0, 127);
  if (index > 79) {
    index = 79;
  }
  return static_cast<float>(kBodyFilter[index]) * (1.0f / 4096.0f);
}

}  // namespace

void BlownEngine::Init(BufferAllocator* allocator) {
  bore_ = allocator->Allocate<float>(kBlownBoreLength);
  Reset();
}

void BlownEngine::Reset() {
  // DigitalOscillator::Init() zeroes the whole state block and Render() calls
  // it on every shape change (digital_oscillator.cc:111-115), so Braids' BLOWN
  // always starts from silence with an empty bore -- there is no non-zero
  // resting state to reproduce here (the saw-swarm finding from wave A). The
  // arena is shared, so the line has to be cleared on every engine switch and
  // not only at Init (SPEC R15).
  if (bore_) {
    for (size_t i = 0; i < kBlownBoreLength; ++i) {
      bore_[i] = 0.0f;
    }
  }
  delay_pointer_ = 0;
  lp_state_ = 0.0f;
  body_state_ = 0.0f;
  body_state_aux_ = 0.0f;
  noise_depth_ = kBlownBreathPressure;
  body_coefficient_ = 0.05f;
  blow_ = kBlownBreathPressure;
  reed_offset_ = kBlownReedOffset;
  rng_state_ = 0x8f2c1d3bu;
  for (size_t i = 0; i < 32; ++i) {
    history_[i] = 0.0f;
    history_aux_[i] = 0.0f;
  }
  history_write_ = 0;
  out_dc_input_ = 0.0f;
  out_dc_output_ = 0.0f;
  aux_dc_input_ = 0.0f;
  aux_dc_output_ = 0.0f;
}

void BlownEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (!bore_) {
    // Allocate<T> returns NULL silently when the arena is exhausted (R15).
    for (size_t i = 0; i < size; ++i) {
      out[i] = 0.0f;
      aux[i] = 0.0f;
    }
    return;
  }

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // Braids' strike_ handling flushes the bore and NOTHING else
    // (digital_oscillator.cc:1311-1314): the breath pressure is static, so the
    // model has no envelope to restart, and lp_state / filter_state / the
    // delay pointer are all deliberately carried across the strike. The port
    // matches that -- clearing them too would start the bore from a state the
    // module never reaches (the saw-swarm finding from wave A, in reverse).
    for (size_t i = 0; i < kBlownBoreLength; ++i) {
      bore_[i] = 0.0f;
    }
  }

  const bool stereo = PLAITS_STEREO_BLOWN && parameters.stereo;

  // R5: the internal rate is 96 kHz, two steps per output sample, so Braids'
  // `(delay_ >> 1) - (1 << 16)` -- half a period less one sample -- transfers
  // as written. `frequency` is cycles per OUTPUT sample, so one period is
  // 2 / frequency internal samples.
  const float frequency = max(1e-6f, NoteToFrequency(parameters.note));
  float delay = 1.0f / frequency - 1.0f;
  // Braids' octave fold for bores too long for the line (1317-1319).
  int guard = 0;
  while (guard < 16 &&
         delay > static_cast<float>(kBlownBoreLength - 1)) {
    delay *= 0.5f;
    ++guard;
  }
  CONSTRAIN(delay, 4.0f, static_cast<float>(kBlownBoreLength - 2));

  const uint32_t base_delay_integral = static_cast<uint32_t>(delay);
  const float base_delay_fractional =
      delay - static_cast<float>(base_delay_integral);

  float pickup = delay * kBlownStereoPickup;
  CONSTRAIN(pickup, 2.0f, static_cast<float>(kBlownBoreLength - 2));
  const uint32_t base_pickup_integral = static_cast<uint32_t>(pickup);
  const float base_pickup_fractional =
      pickup - static_cast<float>(base_pickup_integral);

  // TIMBRE, line 1322: `28000 - (parameter_[0] >> 1)`, applied at 1334 as
  // `Random::GetSample() * parameter >> 15`. The >> 1 is reproduced because
  // the parameter is an int16 on the module.
  const int timbre_code = static_cast<int>(parameters.timbre * 32767.0f);
  const float target_noise_depth = \
      (28000.0f - static_cast<float>(timbre_code >> 1)) * (1.0f / 32768.0f);
  const float target_body = BodyCoefficient(
      parameters.note, parameters.harmonics);

  // The two macros Braids does not have, both stock at noon.
  const float target_blow = ApplyMacro(
      kBlownBreathPressure, kBlownBlowMin, kBlownBlowMax, parameters.morph);
  const float target_reed_offset = ApplyMacro(
      kBlownReedOffset, kBlownReedMin, kBlownReedMax, parameters.macro);

  ParameterInterpolator noise_modulation(
      &noise_depth_, target_noise_depth, size);
  ParameterInterpolator body_modulation(&body_coefficient_, target_body, size);
  ParameterInterpolator blow_modulation(&blow_, target_blow, size);
  ParameterInterpolator reed_modulation(
      &reed_offset_, target_reed_offset, size);

  size_t sample_index = 0;
  while (size--) {
    uint32_t delay_integral = base_delay_integral;
    float delay_fractional = base_delay_fractional;
    uint32_t pickup_integral = base_pickup_integral;
    float pickup_fractional = base_pickup_fractional;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      const float instantaneous_frequency = max(
          1e-6f, frequency + parameters.frequency_offset[sample_index]);
      float instantaneous_delay = 1.0f / instantaneous_frequency - 1.0f;
      int instantaneous_guard = 0;
      while (instantaneous_guard < 16 &&
             instantaneous_delay > static_cast<float>(kBlownBoreLength - 1)) {
        instantaneous_delay *= 0.5f;
        ++instantaneous_guard;
      }
      CONSTRAIN(
          instantaneous_delay,
          4.0f,
          static_cast<float>(kBlownBoreLength - 2));
      delay_integral = static_cast<uint32_t>(instantaneous_delay);
      delay_fractional =
          instantaneous_delay - static_cast<float>(delay_integral);
      float instantaneous_pickup = instantaneous_delay * kBlownStereoPickup;
      CONSTRAIN(
          instantaneous_pickup,
          2.0f,
          static_cast<float>(kBlownBoreLength - 2));
      pickup_integral = static_cast<uint32_t>(instantaneous_pickup);
      pickup_fractional =
          instantaneous_pickup - static_cast<float>(pickup_integral);
    }
#endif
    const float noise_depth = noise_modulation.Next();
    const float body_coefficient = body_modulation.Next();
    const float blow = blow_modulation.Next();
    const float reed_offset = reed_modulation.Next();

    for (int j = 0; j < 2; ++j) {
      // Breath: a static pressure multiplied by white noise, 1334-1336.
      const float breath = blow * \
          (1.0f + NextNoise(&rng_state_) * noise_depth);

      // The fractional read, 1338-1342. delay_pointer_ is the NEXT write, so
      // bore_[delay_pointer_ - n] is n samples old.
      const uint32_t read = \
          delay_pointer_ + 2 * kBlownBoreLength - delay_integral;
      const float a = bore_[read & (kBlownBoreLength - 1)];
      const float b = bore_[(read - 1) & (kBlownBoreLength - 1)];
      const float bore_value = a + (b - a) * delay_fractional;

      // The in-loop half-sample averager, 1344-1345. Its zero sits at the
      // INTERNAL Nyquist, which is why the internal rate had to match.
      float pressure_delta = 0.5f * (bore_value + lp_state_);
      lp_state_ = bore_value;

      // Inverting bore reflection, 1347.
      pressure_delta *= kBlownReflection;
      pressure_delta -= breath;

      // The reed characteristic, 1349-1351: a clipped line, multiplying the
      // pressure difference.
      float reed = kBlownReedSlope * pressure_delta + reed_offset;
      CONSTRAIN(reed, -kBlownClip, kBlownClip);
      float bore_input = pressure_delta * reed + breath;
      CONSTRAIN(bore_input, -kBlownClip, kBlownClip);

      // A second pickup further down the bore, read before the write so it
      // sees the same line state the main tap did. Not in Braids -- it exists
      // only to give the stereo render two genuinely different points on one
      // standing wave.
      float pickup_value = 0.0f;
      if (stereo) {
        const uint32_t pickup_read = \
            delay_pointer_ + 2 * kBlownBoreLength - pickup_integral;
        const float pa = bore_[pickup_read & (kBlownBoreLength - 1)];
        const float pb = bore_[(pickup_read - 1) & (kBlownBoreLength - 1)];
        pickup_value = pa + (pb - pa) * pickup_fractional;
      }

      bore_[delay_pointer_ & (kBlownBoreLength - 1)] = bore_input;
      ++delay_pointer_;

      // The body filter, 1355-1356. Output only -- it is never written back
      // into the bore, so COLOR equalises the sound without changing the
      // pipe's harmonic content.
      body_state_ += body_coefficient * (bore_input - body_state_);

      float aux_sample;
      if (stereo) {
        body_state_aux_ += body_coefficient * (pickup_value - body_state_aux_);
        aux_sample = body_state_aux_;
      } else {
        aux_sample = bore_input * kBlownAuxTrim;
      }

      history_[history_write_ & 31] = body_state_;
      history_aux_[history_write_ & 31] = aux_sample;
      ++history_write_;
    }

    // 31-tap halfband, centred 15 internal samples behind the newest.
    const uint32_t centre = history_write_ - 16;
    float decimated = kHalfbandCentre * history_[centre & 31];
    float decimated_aux = kHalfbandCentre * history_aux_[centre & 31];
    for (int k = 0; k < 8; ++k) {
      const uint32_t offset = static_cast<uint32_t>(2 * k + 1);
      decimated += kHalfband[k] * (history_[(centre - offset) & 31] +
                                   history_[(centre + offset) & 31]);
      decimated_aux += kHalfband[k] * (history_aux_[(centre - offset) & 31] +
                                       history_aux_[(centre + offset) & 31]);
    }

    // The static breath pressure leaves the bore parked off zero -- the loop's
    // fixed point predicts 0.023 and the reference measures 0.015 to 0.018 --
    // which Braids emits and a Plaits engine must not. R1 then applies: an
    // engine with a DC blocker cannot pin its post-blocker peak, so this
    // engine registers negative gains and takes the limiter path.
    ONE_POLE(out_dc_input_, decimated, kBlownDcCoefficient);
    ONE_POLE(aux_dc_input_, decimated_aux, kBlownDcCoefficient);
    out_dc_output_ = decimated - out_dc_input_;
    aux_dc_output_ = decimated_aux - aux_dc_input_;

    *out++ = out_dc_output_;
    *aux++ = aux_dc_output_;
    ++sample_index;
  }
}

}  // namespace plaits
