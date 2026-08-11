// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' PRTC: three fixed-interval resonant filters excited by a shared
// decaying noise burst.

#include "plaits/dsp/engine2/particle_burst_engine.h"

#include <algorithm>

#include "plaits/build_config.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

inline float ParticleFastLog2(float x) {
  union { float f; uint32_t i; } u = { x };
  const float e = static_cast<float>((u.i >> 23) & 0xffu) - 127.0f;
  u.i = (u.i & 0x007fffffu) | 0x3f800000u;
  const float m = u.f;
  return e + (-1.7417939f + (2.8212026f + (-1.4699568f
      + (0.44717955f - 0.056570851f * m) * m) * m) * m);
}

// Braids' lut_resonator_coefficient and lut_resonator_scale
// (braids/resources.cc:44-115), 129 uint16 entries each, one per MIDI note
// 0-128. Both are reproduced from braids/resources/lookup_tables.py:59-90 --
// coefficient from the closed form `2*cos(2*pi*f)*32768` with
// `f = min(440*2^((i-69)/12) / 48000, 0.25)`, scale from the 2000-sample
// analytic impulse-response simulation at a CALIBRATION resonance of 0.99985
// -- and both reproduce the embedded ints at 0 LSB (verified
// programmatically, not retyped).
//
// BOTH ARE EMBEDDED AT BRAIDS' OWN uint16 WIDTH, and read back through the
// integer Interpolate824 below rather than as full-precision floats. That is
// not pedantry: `lut_resonator_coefficient` is `2*cos(theta) * 32768`, so
// near theta = 0 it sits just under 65536 and one integer LSB is an enormous
// step in POLE ANGLE. At MIDI 57 (the +12 filter over a note-45 root) the
// table's own truncation puts Braids' resonator at 227.5 Hz, not the nominal
// 220 Hz -- +74 cents -- and at MIDI 33 it is +623 cents. Evaluating the
// closed form in float instead "corrects" a mistuning that is audibly part of
// the model, so the port keeps the table and Braids' exact
// `c * kResonanceFactor >> 15` truncation on top of it. The scale table's
// truncation matters for the same reason at the level end: at MIDI 20 the
// exact gain is 1.96 and Braids stores 1, a 6 dB difference.
const uint16_t kParticleResonatorCoefficient[129] = {
  65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535,
  65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535,
  65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535,
  65535, 65535, 65535, 65535, 65535, 65534, 65534, 65534,
  65534, 65534, 65534, 65533, 65533, 65533, 65532, 65532,
  65532, 65531, 65531, 65530, 65529, 65529, 65528, 65527,
  65526, 65525, 65523, 65522, 65520, 65518, 65516, 65514,
  65511, 65508, 65505, 65501, 65497, 65492, 65487, 65481,
  65475, 65467, 65459, 65449, 65439, 65427, 65414, 65399,
  65382, 65363, 65342, 65318, 65292, 65262, 65228, 65191,
  65149, 65101, 65048, 64988, 64922, 64847, 64762, 64668,
  64562, 64443, 64310, 64160, 63992, 63804, 63593, 63356,
  63091, 62794, 62461, 62088, 61670, 61202, 60677, 60091,
  59435, 58701, 57881, 56964, 55941, 54799, 53526, 52107,
  50528, 48773, 46824, 44662, 42268, 39623, 36704, 33492,
  29966, 26107, 21898, 17325, 12380,  7061,  1374,     0,
      0,
};

const uint16_t kParticleResonatorScale[129] = {
      1,     1,     1,     1,     1,     1,     1,     1,
      1,     1,     1,     1,     1,     1,     1,     1,
      1,     1,     1,     1,     1,     2,     2,     2,
      2,     2,     3,     3,     3,     4,     4,     4,
      5,     5,     6,     6,     7,     8,     8,     9,
     10,    11,    12,    13,    14,    16,    17,    19,
     20,    22,    24,    27,    29,    32,    35,    38,
     41,    45,    49,    54,    58,    64,    70,    76,
     83,    90,    98,   107,   117,   128,   139,   152,
    166,   181,   197,   215,   234,   255,   256,   256,
    256,   256,   256,   256,   256,   256,   256,   256,
    256,   256,   256,   256,   256,   256,   256,   256,
    256,   256,   256,   256,   256,   256,   256,   256,
    256,   256,   256,   256,   256,   256,   256,   256,
    256,   256,   256,   256,   256,   256,   256,   256,
    256,   256,   256,   256,   256,   256,   256,   256,
    256,
};

// Braids' kResonanceFactor, at the value the module's `static const int32_t`
// initialiser actually truncates to (digital_oscillator.cc:2078):
// 32768 * 0.996 = 32636.928 -> 32636. Its companion kResonanceSquared
// (32768 * 0.996 * 0.996 = 32506.35 -> 32506) is used directly in the
// recursion and lives in the header as kParticleResonanceSquared.
const int32_t kParticleResonanceFactorI16 = 32636;

// stmlib's Interpolate824 for a uint16 table (stmlib/utils/dsp.h:100), driven
// by `p << 17` exactly as digital_oscillator.cc:2110 does -- so the top 8 bits
// of the phase index the table by MIDI note and the next 16 carry p's
// 1/128-semitone fraction. The unsigned `(b - a)` wraps on a descending table
// (the coefficient table is monotonically falling); that wrap is Braids' own
// behaviour and cancels exactly in the uint16 result.
inline uint32_t ParticleInterpolate824(const uint16_t* table, int32_t p) {
  const uint32_t phase = static_cast<uint32_t>(p) << 17;
  const uint32_t index = phase >> 24;
  const uint32_t a = table[index];
  const uint32_t b = table[index + 1];
  const uint32_t fractional = (phase >> 8) & 0xffffu;
  return (a + (((b - a) * fractional) >> 16)) & 0xffffu;
}

// digital_oscillator.cc:2110-2111 (and :2116, :2121 for the other two
// filters): look the note up in Braids' coefficient table, then fold in the
// module's fixed resonance with the same truncating `>> 15`. Returned in the
// port's float units (Braids' Q15 value / 32768).
inline float ResonatorCoefficient(int32_t p) {
  const int32_t raw = static_cast<int32_t>(
      ParticleInterpolate824(kParticleResonatorCoefficient, p));
  return static_cast<float>((raw * kParticleResonanceFactorI16) >> 15) /
      32768.0f;
}

inline float ResonatorScale(int32_t p) {
  return static_cast<float>(
      ParticleInterpolate824(kParticleResonatorScale, p));
}

}  // namespace

void ParticleBurstEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void ParticleBurstEngine::Reset() {
  for (int channel = 0; channel < 2; ++channel) {
    amplitude_[channel] = 0.0f;
    y1_state_[channel][0] = y1_state_[channel][1] = 0.0f;
    y2_state_[channel][0] = y2_state_[channel][1] = 0.0f;
    y3_state_[channel][0] = y3_state_[channel][1] = 0.0f;
    c1_[channel] = c2_[channel] = c3_[channel] = 0.0f;
    s1_[channel] = s2_[channel] = s3_[channel] = 0.0f;
  }
}

void ParticleBurstEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  const bool stereo = PLAITS_STEREO_PARTICLE_BURST && parameters.stereo;

  // TIMBRE: density, the trigger probability per sample
  // (digital_oscillator.cc:2085,2104).
  const uint32_t density = static_cast<uint32_t>(
      kParticleDensityFloor + parameters.timbre * kParticleDensityRange);

  // COLOR: jitter magnitude, scaled to Braids' int16 parameter_[1] range so
  // the >>17/>>15/>>16 divisors below are the SAME numbers the source uses
  // (digital_oscillator.cc:2108,2114,2119).
  const float harmonics_scaled = parameters.harmonics * 32767.0f;

  // MORPH: Chord width. 1.0 at MORPH 0.5 is Braids' own voicing exactly;
  // MORPH has no detent (R11), but this engine still treats 0.5 as its
  // stock position for A/B purposes, the same way fold treats Symmetry.
  const float chord_width = parameters.morph * 2.0f;

  // MACRO: Decay, ApplyMacro on the same raw per-sample coefficient Braids
  // hardcodes (digital_oscillator.cc:2076,2129), stock at 0.5.
  const float decay = ApplyMacro(
      kParticleStockDecay, kParticleMinDecay, kParticleMaxDecay,
      parameters.macro);

  // The single upfront pitch-correction shift (see the header's PITCH
  // CORRECTION note) -- everything below stays in this one note-space.
  const float pitch128 = (parameters.note + kParticlePitchCorrection) * 128.0f;
  float pitch_offset128[kMaxBlockSize];
  fill(&pitch_offset128[0], &pitch_offset128[size], 0.0f);
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  if (parameters.frequency_offset) {
    const float base_frequency = NoteToFrequency(parameters.note);
    for (size_t i = 0; i < size; ++i) {
      const float instantaneous_frequency = max(
          1e-7f, base_frequency + parameters.frequency_offset[i]);
      pitch_offset128[i] = 1536.0f * ParticleFastLog2(
          instantaneous_frequency / max(base_frequency, 1e-7f));
    }
  }
#endif

  const float offset1 = kParticleFilter1Interval * 128.0f * chord_width;
  const float offset2 = kParticleFilter2Interval * 128.0f * chord_width;
  const float offset3 = kParticleFilter3Interval * 128.0f * chord_width;

  const int num_channels = stereo ? 2 : 1;

  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);

  for (int channel = 0; channel < num_channels; ++channel) {
    float amplitude = amplitude_[channel];
    float y11 = y1_state_[channel][0];
    float y12 = y1_state_[channel][1];
    float y21 = y2_state_[channel][0];
    float y22 = y2_state_[channel][1];
    float y31 = y3_state_[channel][0];
    float y32 = y3_state_[channel][1];
    float c1 = c1_[channel];
    float c2 = c2_[channel];
    float c3 = c3_[channel];
    float s1 = s1_[channel];
    float s2 = s2_[channel];
    float s3 = s3_[channel];

    for (size_t i = 0; i < size; ++i) {
      // One RNG draw per sample, reused for the trigger test, the jitter
      // (on a trigger) and the excitation sample itself -- exactly Braids'
      // budget (digital_oscillator.cc:2103). The right channel's draws
      // happen only in this stereo-only loop iteration, so turning stereo
      // on never perturbs the mono/left sequence (R14).
      const uint32_t noise = Random::GetWord();

      if ((noise & kParticleDensityMask) < density) {
        amplitude = 1.0f;

        const float noise_a = static_cast<float>(
            static_cast<int32_t>(noise & 0x0fffu) - 0x800);
        const float noise_b = static_cast<float>(
            static_cast<int32_t>((noise >> 15) & 0x1fffu) - 0x1000);

        // Braids keeps p in its 14-bit, 128-counts-per-semitone pitch space
        // and looks the table up there (digital_oscillator.cc:2108-2121), so
        // the port truncates back onto the same integer grid before the
        // lookup rather than interpolating a float note. One integer LSB of
        // the coefficient table is worth far more than one count of p at low
        // registers, so this grid is not a detail to smooth over.
        int32_t p1 = static_cast<int32_t>(pitch128 + pitch_offset128[i] + offset1 +
            3.0f * noise_a * harmonics_scaled / kParticleJitter1Divisor);
        CONSTRAIN(p1, 0, 16383);
        c1 = ResonatorCoefficient(p1);
        s1 = ResonatorScale(p1);

        int32_t p2 = static_cast<int32_t>(pitch128 + pitch_offset128[i] + offset2 +
            noise_a * harmonics_scaled / kParticleJitter2Divisor);
        CONSTRAIN(p2, 0, 16383);
        c2 = ResonatorCoefficient(p2);
        s2 = ResonatorScale(p2);

        int32_t p3 = static_cast<int32_t>(pitch128 + pitch_offset128[i] + offset3 +
            noise_b * harmonics_scaled / kParticleJitter3Divisor);
        CONSTRAIN(p3, 0, 16383);
        c3 = ResonatorCoefficient(p3);
        s3 = ResonatorScale(p3);
      }

      // digital_oscillator.cc:2128 reads `static_cast<int16_t>(noise)`, the
      // LOW 16 bits of the same word, signed -- a different slice again
      // from the trigger test (low 23 bits) and the jitter (bits 0-11 and
      // 15-27). `sample` is the raw decaying-noise excitation, and doubles
      // as this channel's AUX in mono.
      const float sample = static_cast<float>(
          static_cast<int16_t>(noise & 0xffffu)) / 32768.0f * amplitude;
      amplitude *= decay;

      // The three resonators (digital_oscillator.cc:2131-2157). `s` is
      // Braids' own uint16 scale, so the `>>16` scale-multiply stays
      // `/ 65536.0f`; the sign-safe abs() dance Braids uses to avoid
      // arithmetic-shift bias on a negative dividend has no float equivalent
      // to reproduce (a declared deviation, see the header) because float
      // multiplication is already exact either way.
      float y10 = sample * s1 / 65536.0f + y11 * c1 - y12 * kParticleResonanceSquared;
      CONSTRAIN(y10, -1.0f, 1.0f);
      y12 = y11;
      y11 = y10;

      float y20 = sample * s2 / 65536.0f + y21 * c2 - y22 * kParticleResonanceSquared;
      CONSTRAIN(y20, -1.0f, 1.0f);
      y22 = y21;
      y21 = y20;

      float y30 = sample * s3 / 65536.0f + y31 * c3 - y32 * kParticleResonanceSquared;
      CONSTRAIN(y30, -1.0f, 1.0f);
      y32 = y31;
      y31 = y30;

      // digital_oscillator.cc:2159-2160: sum, then clip again. Bounded sum
      // of three individually-bounded terms -- R1/R2 -- so no limiter is
      // required at the postProcessing layer.
      float sum = y10 + y20 + y30;
      CONSTRAIN(sum, -1.0f, 1.0f);

      if (channel == 0) {
        out[i] = sum;
        // Mono AUX is the raw exciter; overwritten below if stereo.
        aux[i] = sample;
      } else {
        aux[i] = sum;
      }
    }

    amplitude_[channel] = amplitude;
    y1_state_[channel][0] = y11;
    y1_state_[channel][1] = y12;
    y2_state_[channel][0] = y21;
    y2_state_[channel][1] = y22;
    y3_state_[channel][0] = y31;
    y3_state_[channel][1] = y32;
    c1_[channel] = c1;
    c2_[channel] = c2;
    c3_[channel] = c3;
    s1_[channel] = s1;
    s2_[channel] = s2;
    s3_[channel] = s3;
  }
}

}  // namespace plaits
