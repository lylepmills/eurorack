// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' VOWL: three table-read formant oscillators, a per-period ramp and a
// tanh shaper.

#include "plaits/dsp/engine2/vowel_engine.h"

#include "plaits/build_config.h"

#include "stmlib/dsp/dsp.h"

namespace plaits {

using namespace stmlib;

namespace {

// Braids' wav_formant_sine, verbatim from braids/resources.cc, reproduced at
// 0 LSB of deviation across all 256 entries from the generator in
// braids/resources/waveforms.py:42-54:
//
//   gains  = exp(0.184 * arange(16)); gains[0] = 0
//   sine   = sin(arange(16) / 16 * 2 * pi)
//   values = round(gains * sine[i] * 4)
//
// laid out as [phase_step * 16 + amplitude_index], which is exactly how
// digital_oscillator.cc:511 indexes it: `phaselet | formant_amplitude` with
// `phaselet = (phase >> 24) & 0xf0`.
//
// Held as int8 rather than Braids' int16 -- the measured range is -63..63, so
// no value changes and the table costs 256 bytes instead of 512.
//
// The amplitude quantisation is the point of embedding this rather than
// evaluating it: the row for amplitude index 1 is {0, +-2, +-3, +-4, +-5},
// three bits of resolution on a quiet formant, and reconstructing it from the
// float closed form would smooth away what the model actually sounds like.

const int8_t kFormantSine[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   2,   2,   3,   3,   4,   5,   6,   7,   8,  10,  12,  14,  17,  20,  24,
    0,   3,   4,   5,   6,   7,   9,  10,  12,  15,  18,  21,  26,  31,  37,  45,
    0,   4,   5,   6,   8,   9,  11,  13,  16,  19,  23,  28,  34,  40,  49,  58,
    0,   5,   6,   7,   8,  10,  12,  15,  17,  21,  25,  30,  36,  44,  53,  63,
    0,   4,   5,   6,   8,   9,  11,  13,  16,  19,  23,  28,  34,  40,  49,  58,
    0,   3,   4,   5,   6,   7,   9,  10,  12,  15,  18,  21,  26,  31,  37,  45,
    0,   2,   2,   3,   3,   4,   5,   6,   7,   8,  10,  12,  14,  17,  20,  24,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,  -2,  -2,  -3,  -3,  -4,  -5,  -6,  -7,  -8, -10, -12, -14, -17, -20, -24,
    0,  -3,  -4,  -5,  -6,  -7,  -9, -10, -12, -15, -18, -21, -26, -31, -37, -45,
    0,  -4,  -5,  -6,  -8,  -9, -11, -13, -16, -19, -23, -28, -34, -40, -49, -58,
    0,  -5,  -6,  -7,  -8, -10, -12, -15, -17, -21, -25, -30, -36, -44, -53, -63,
    0,  -4,  -5,  -6,  -8,  -9, -11, -13, -16, -19, -23, -28, -34, -40, -49, -58,
    0,  -3,  -4,  -5,  -6,  -7,  -9, -10, -12, -15, -18, -21, -26, -31, -37, -45,
    0,  -2,  -2,  -3,  -3,  -4,  -5,  -6,  -7,  -8, -10, -12, -14, -17, -20, -24
};

// wav_formant_square, factorised. Braids stores a full 256-entry table, but
// the generator (waveforms.py:50-53) is `amps = round(gains)` with the sign
// flipped for the upper half of the cycle, so the table is exactly
// `(phase_step < 8 ? +1 : -1) * gain[amplitude]`. Verified against every one
// of the 256 entries in braids/resources.cc before dropping to 16 -- the
// factorisation is exact, not approximate.
//
// This is the model's third formant, and it being a SQUARE is one of the four
// things that survive between VOWL and its Plaits descendant.
const int8_t kFormantSquareGain[16] = {
   0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  6,  8,  9, 11, 13, 16
};

// ws_moderate_overdrive, verbatim from braids/resources.cc, reproduced at
// 0 LSB across all 257 entries from braids/resources/waveshapers.py:40,62:
// `scale(tanh(2x))` over x in [-1, 1] with the last point repeated, centred
// and normalised to +-32766.
//
// The stored curve is asymmetric -- it runs -32766..32728, because `scale`
// normalises on the larger side after centring. Braids leaves that asymmetry
// in the signal and so does the port; it is part of why the model needs a DC
// blocker that the module does not have.
//
// Embedded rather than evaluated: `tanh` in an inner loop that runs at 96 kHz
// is not a trade worth making, and the table's own quantisation is audible on
// the quiet parts of the ramp.
const int16_t kModerateOverdrive[257] = {
  -32766, -32728, -32689, -32648, -32607, -32564, -32519, -32474,
  -32427, -32378, -32328, -32277, -32224, -32170, -32113, -32056,
  -31996, -31935, -31872, -31807, -31740, -31671, -31600, -31527,
  -31451, -31374, -31294, -31212, -31128, -31041, -30951, -30859,
  -30765, -30667, -30567, -30464, -30358, -30250, -30138, -30022,
  -29904, -29782, -29657, -29529, -29397, -29261, -29122, -28979,
  -28832, -28681, -28526, -28367, -28204, -28036, -27864, -27688,
  -27507, -27321, -27131, -26936, -26736, -26531, -26321, -26106,
  -25886, -25660, -25429, -25192, -24950, -24702, -24449, -24190,
  -23925, -23654, -23377, -23094, -22805, -22510, -22209, -21902,
  -21588, -21268, -20942, -20609, -20270, -19924, -19573, -19215,
  -18850, -18479, -18102, -17718, -17328, -16932, -16530, -16121,
  -15707, -15286, -14859, -14427, -13989, -13545, -13095, -12640,
  -12180, -11715, -11244, -10769, -10289,  -9804,  -9315,  -8822,
   -8324,  -7823,  -7319,  -6810,  -6299,  -5785,  -5268,  -4748,
   -4226,  -3703,  -3177,  -2650,  -2121,  -1592,  -1062,   -531,
       0,    531,   1062,   1592,   2122,   2650,   3177,   3703,
    4227,   4749,   5268,   5785,   6299,   6811,   7319,   7824,
    8325,   8822,   9315,   9804,  10289,  10769,  11244,  11715,
   12180,  12641,  13095,  13545,  13989,  14427,  14860,  15286,
   15707,  16122,  16530,  16933,  17329,  17719,  18102,  18479,
   18850,  19215,  19573,  19925,  20270,  20609,  20942,  21268,
   21588,  21902,  22209,  22510,  22806,  23094,  23377,  23654,
   23925,  24190,  24449,  24703,  24950,  25192,  25429,  25660,
   25886,  26106,  26321,  26531,  26736,  26936,  27131,  27322,
   27507,  27688,  27865,  28037,  28204,  28367,  28526,  28681,
   28832,  28979,  29122,  29262,  29397,  29529,  29658,  29783,
   29904,  30023,  30138,  30250,  30359,  30465,  30568,  30668,
   30765,  30860,  30952,  31041,  31128,  31212,  31294,  31374,
   31452,  31527,  31600,  31671,  31740,  31807,  31872,  31935,
   31996,  32056,  32114,  32170,  32224,  32277,  32329,  32379,
   32427,  32474,  32520,  32564,  32607,  32648,  32689,  32728,
   32728
};

// The phoneme tables, lifted from digital_oscillator.cc:445-466 (vowels_data
// and consonant_data) as flat [frame * 3 + formant] arrays. Frequencies are
// the raw bytes Braids multiplies by 0x1000 * formant_shift; amplitudes are
// the 4-bit indices into the tables above.
//
// Nine vowel frames and eight consonant frames. TIMBRE crossfades between
// adjacent vowel frames; the consonants are only reachable by triggering.
const uint8_t kVowelFrequency[27] = {
   27,  40,  89,
   18,  51,  62,
   15,  69,  93,
   10,  84, 110,
   23,  44,  87,
   13,  29,  80,
    6,  46,  81,
    9,  51,  95,
    6,  73,  99
};

const uint8_t kVowelAmplitude[27] = {
  15, 13,  1,
  13, 12,  6,
  14, 12,  7,
  13, 10,  8,
  15, 12,  1,
  13,  8,  0,
  12,  3,  0,
  15,  3,  0,
   7,  3, 14
};

const uint8_t kConsonantFrequency[24] = {
    6,  54, 121,
   18,  50,  51,
   11,  24,  70,
   15,  69,  74,
   16,  37, 111,
   18,  51,  62,
    6,  26,  81,
    6,  73,  99
};

const uint8_t kConsonantAmplitude[24] = {
   9,  9,  0,
  12, 10,  5,
  13,  8,  0,
  14, 12,  7,
  14,  8,  1,
  14, 12,  6,
   5,  5,  5,
   7, 10, 14
};

// The 2:1 decimator. 31-tap windowed-sinc halfband, fc = 0.25 of the 96 kHz
// internal rate, Blackman-Harris window, normalised to unity DC gain. A
// halfband puts a zero on every even offset from the centre, so 17 of the 31
// taps are non-zero and symmetry folds those to 9 multiplies per output.
//
// Measured from these coefficients: -0.07 dB at 16 kHz, -0.36 dB at 18 kHz,
// -1.18 dB at 20 kHz, -6.02 dB at 24 kHz, then -17.9 dB at 28 kHz, -41.7 dB
// at 32 kHz, -114 dB at 40 kHz. SPEC R7: the accepted residue is the 28 kHz
// figure, which folds to 20 kHz. The reference renderer uses a 127-tap sinc
// below -90 dB, so the port carries alias energy in the top octave that the
// reference does not -- that difference is expected in the top band of an A/B
// and is not a defect in the model.
const float kHalfband[9] = {
  0.5000007671f,
  0.3103159181f,
  -0.0842284807f,
  0.0331408846f,
  -0.0121862031f,
  0.0036432918f,
  -0.0007726768f,
  0.0000881559f,
  -0.0000012732f
};

// Each 32-sample history is mirrored into a 64-entry buffer. With `write` one
// past the newest sub-sample, `buffer + (write & 31) + 16` is x[n - 15] in a
// contiguous copy of the history. The surviving pairs sit at ODD offsets from
// it: h[14]/h[16] at +-1, h[12]/h[18] at +-3, and so on out to h[0]/h[30] at
// +-15. This preserves the exact accumulation order without masking every tap.
inline float Decimate(const float* buffer, int write) {
  const float* centre =
      buffer + (write & kVowelFirMask) + kVowelFirSize / 2;
  float sum = kHalfband[0] * centre[0];
  for (int k = 1; k < 9; ++k) {
    const int offset = 2 * k - 1;
    sum += kHalfband[k] * (centre[offset] + centre[-offset]);
  }
  return sum;
}

// stmlib::Interpolate88 on the shaper table: the int16 sum becomes a uint16
// index, its top 8 bits pick the entry and its low 8 interpolate toward the
// next. digital_oscillator.cc:529.
inline float ReadOverdrive(int sample) {
  const int index = sample + 32768;
  const int integral = index >> 8;
  const int fractional = index & 0xff;
  const int a = kModerateOverdrive[integral];
  const int b = kModerateOverdrive[integral + 1];
  return static_cast<float>(a + (((b - a) * fractional) >> 8)) *
      (1.0f / 32768.0f);
}

// digital_oscillator.cc:511/515 for the two sine formants and :519 for the
// square one.
inline int ReadFormant(uint32_t phase, int amplitude) {
  return kFormantSine[((phase >> 24) & 0xf0) | amplitude];
}

inline int ReadSquareFormant(uint32_t phase, int amplitude) {
  const int gain = kFormantSquareGain[amplitude];
  return (phase & 0x80000000u) ? -gain : gain;
}

// Braids' increment is a 29-bit integer, which does not survive a float round
// trip exactly, so the unity case short-circuits: at the two new macros' stock
// positions the port advances the formant phases by the module's own integer.
inline uint32_t ScaleIncrement(uint32_t increment, float scale) {
  uint32_t result = increment;
  if (scale != 1.0f) {
    // The largest table-derived increment is 121 * 0x1000 * 711 =
    // 352382976, and Spread plus stereo detune cannot scale it above 2.04.
    // Their product is below 719 million, safely inside uint32_t.
    result = static_cast<uint32_t>(
        static_cast<float>(increment) * scale);
  }
  if (result > kVowelMaxFormantIncrement) {
    result = kVowelMaxFormantIncrement;
  }
  return result;
}

}  // namespace

// stmlib::Random::GetSample(): an LCG whose state starts at 0x21
// (stmlib/utils/random.cc:34), read as the top 16 bits of the new state.
// Kept per instance rather than reaching for the global so that Reset() is
// reproducible.
int VowelEngine::NextRandomSample() {
  rng_state_ = rng_state_ * 1664525u + 1013904223u;
  return static_cast<int16_t>(rng_state_ >> 16);
}

void VowelEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

// DigitalOscillator::Init() (digital_oscillator.h:246) zeroes the state, zeroes
// the phase and sets `strike_ = true`; DigitalOscillator::Render calls it on
// every shape change, so a fresh VOWL ALWAYS starts inside a consonant. The
// port has to start there too -- checking what state Braids reaches on its own
// first sample, rather than picking a plausible-looking rest state, is the
// mistake saw-swarm made.
void VowelEngine::Reset() {
  phase_ = 0;
  phase_increment_ = 0;
  for (int i = 0; i < kVowelNumFormants; ++i) {
    formant_phase_[i] = 0;
    formant_phase_r_[i] = 0;
    formant_increment_[i] = 0;
    formant_amplitude_[i] = 0;
  }
  noise_ = 0;
  consonant_samples_ = 0;
  strike_pending_ = true;
  rng_state_ = 0x21;
  for (int i = 0; i < kVowelFirBufferSize; ++i) {
    fir_a_[i] = 0.0f;
    fir_b_[i] = 0.0f;
  }
  fir_write_ = 0;
  dc_input_ = 0.0f;
  dc_output_ = 0.0f;
  dc_input_aux_ = 0.0f;
  dc_output_aux_ = 0.0f;
}

void VowelEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  // Braids' Strike() only raises the flag (digital_oscillator.h:284) -- it does
  // NOT reset the phase or the formant phases. The consonant is picked in the
  // render function, on the next block.
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    strike_pending_ = true;
  }

  const bool stereo = PLAITS_STEREO_VOWEL && parameters.stereo;

  float note = parameters.note;
  if (note > kVowelHighestNote) {
    note = kVowelHighestNote;
  }
  // The inner loop runs at 96 kHz, so the pitch increment is half a 48 kHz one.
  phase_increment_ = static_cast<uint32_t>(
      NoteToFrequency(note) * 0.5f * 4294967296.0f);

  // Braids' two knobs, reconstructed in their integer form so that the frame
  // selection and the shift quantise exactly as the module's do. The renderer
  // feeds MacroOscillator `(int16_t)(normalized * 32767)`.
  int p_vowel = static_cast<int>(parameters.morph * 32767.0f);
  int p_shift = static_cast<int>(parameters.timbre * 32767.0f);
  CONSTRAIN(p_vowel, 0, 32767);
  CONSTRAIN(p_shift, 0, 32767);
  const int vowel_index = p_vowel >> 12;
  const int balance = p_vowel & 0x0fff;
  const int formant_shift = kVowelMinFormantShift + (p_shift >> 6);

  const float spread = ApplyMacro(
      1.0f, kVowelMinSpread, kVowelMaxSpread, parameters.harmonics);
  const float grain = ApplyMacro(
      1.0f, kVowelMinGrain, kVowelMaxGrain, parameters.macro);
  // At MACRO's detent ApplyMacro returns exactly 1.0f, so this is exactly
  // 1.0f and the envelope below reduces to Braids' `255 - (phase >> 24)`.
  const float inverse_grain = 1.0f / grain;

  // Braids' per-block frame update, digital_oscillator.cc:476-502. Note the
  // consonant branch deliberately does NOT refresh the increments, so a COLOR
  // move during a consonant does nothing until the vowel returns -- the port
  // keeps that.
  if (strike_pending_) {
    strike_pending_ = false;
    consonant_samples_ = kVowelConsonantSamples;
    const int index = (NextRandomSample() + 1) & 7;
    for (int i = 0; i < kVowelNumFormants; ++i) {
      formant_increment_[i] = static_cast<uint32_t>(
          kConsonantFrequency[index * kVowelNumFormants + i]) * 0x1000u *
          static_cast<uint32_t>(formant_shift);
      formant_amplitude_[i] =
          kConsonantAmplitude[index * kVowelNumFormants + i];
    }
    // Consonants 6 and 7 are the unvoiced ones: they jitter the period reset
    // instead of letting it land on the pitch. digital_oscillator.cc:486.
    noise_ = index >= 6 ? 4095 : 0;
  }

  if (consonant_samples_ > 0) {
    consonant_samples_ -= kVowelOversampling * static_cast<int>(size);
    if (consonant_samples_ < 0) {
      consonant_samples_ = 0;
    }
  } else {
    for (int i = 0; i < kVowelNumFormants; ++i) {
      const int a = kVowelFrequency[vowel_index * kVowelNumFormants + i];
      const int b = kVowelFrequency[(vowel_index + 1) * kVowelNumFormants + i];
      formant_increment_[i] = static_cast<uint32_t>(
          a * (0x1000 - balance) + b * balance) *
          static_cast<uint32_t>(formant_shift);
      const int ampl_a = kVowelAmplitude[vowel_index * kVowelNumFormants + i];
      const int ampl_b =
          kVowelAmplitude[(vowel_index + 1) * kVowelNumFormants + i];
      // A TRUNCATED integer index, not a float gain: the amplitude ladder is
      // baked into the waveform tables at 16 steps, so the crossfade between
      // two frames steps rather than glides.
      formant_amplitude_[i] =
          (ampl_a * (0x1000 - balance) + ampl_b * balance) >> 12;
    }
    noise_ = 0;
  }

  uint32_t increment_l[kVowelNumFormants];
  uint32_t increment_r[kVowelNumFormants];
  for (int i = 0; i < kVowelNumFormants; ++i) {
    const float shape = i == 0 ? 1.0f : spread;
    increment_l[i] = ScaleIncrement(formant_increment_[i],
        stereo ? shape * (1.0f - kVowelStereoDetune) : shape);
    increment_r[i] = ScaleIncrement(formant_increment_[i],
        shape * (1.0f + kVowelStereoDetune));
  }

  const int amplitude_0 = formant_amplitude_[0];
  const int amplitude_1 = formant_amplitude_[1];
  const int amplitude_2 = formant_amplitude_[2];

  for (size_t i = 0; i < size; ++i) {
    uint32_t phase_increment = phase_increment_;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      float modulated_increment = static_cast<float>(phase_increment_) +
          parameters.frequency_offset[i] * 0.5f * 4294967296.0f;
      CONSTRAIN(
          modulated_increment,
          0.0f,
          static_cast<float>(kVowelMaxFormantIncrement));
      phase_increment = static_cast<uint32_t>(modulated_increment);
    }
#endif
    for (int t = 0; t < kVowelOversampling; ++t) {
      phase_ += phase_increment;

      formant_phase_[0] += increment_l[0];
      formant_phase_[1] += increment_l[1];
      formant_phase_[2] += increment_l[2];
      int stack = ReadFormant(formant_phase_[0], amplitude_0) +
          ReadFormant(formant_phase_[1], amplitude_1) +
          ReadSquareFormant(formant_phase_[2], amplitude_2);

      int stack_r = 0;
      if (stereo) {
        formant_phase_r_[0] += increment_r[0];
        formant_phase_r_[1] += increment_r[1];
        formant_phase_r_[2] += increment_r[2];
        stack_r = ReadFormant(formant_phase_r_[0], amplitude_0) +
            ReadFormant(formant_phase_r_[1], amplitude_1) +
            ReadSquareFormant(formant_phase_r_[2], amplitude_2);
      }

      // Braids: `sample *= 255 - (phase_ >> 24)`. Grain divides the ramp's
      // slope; at the detent inverse_grain is exactly 1.0f and the cast
      // reproduces the shift result bit for bit.
      int envelope = 255 - static_cast<int>(
          static_cast<float>(phase_ >> 24) * inverse_grain);
      CONSTRAIN(envelope, 0, 255);

      int sample = stack * envelope;
      int sample_r = stack_r * envelope;

      // The RNG is drawn every sample whether or not the jitter is armed, so
      // that the port's sequence stays in step with the module's.
      const int32_t phase_noise = NextRandomSample() * noise_;
      if (static_cast<uint32_t>(phase_ + static_cast<uint32_t>(phase_noise)) <
          phase_increment_) {
        formant_phase_[0] = 0;
        formant_phase_[1] = 0;
        formant_phase_[2] = 0;
        formant_phase_r_[0] = 0;
        formant_phase_r_[1] = 0;
        formant_phase_r_[2] = 0;
        sample = 0;
        sample_r = 0;
      }

      // int16 headroom: the largest stack over every reachable amplitude
      // triple is 108 and 108 * 255 = 27540, so neither Braids' int16 nor this
      // int wraps. Measured, not assumed.
      const int fir_index = fir_write_ & kVowelFirMask;
      const float fir_a = ReadOverdrive(sample);
      const float fir_b = stereo
          ? ReadOverdrive(sample_r)
          : static_cast<float>(sample) * (1.0f / 32768.0f);
      fir_a_[fir_index] = fir_a;
      fir_a_[fir_index + kVowelFirSize] = fir_a;
      fir_b_[fir_index] = fir_b;
      fir_b_[fir_index + kVowelFirSize] = fir_b;
      ++fir_write_;
    }

    const float decimated = Decimate(fir_a_, fir_write_);
    const float decimated_aux = Decimate(fir_b_, fir_write_);

    // The shaper is asymmetric by construction and the ramp is one-sided, so
    // the stack carries DC that Braids simply passes on. SPEC R1: an engine
    // with a DC blocker cannot pin its post-blocker peak, so this one declares
    // negative gains and takes the limiter path.
    const float dc_out = decimated - dc_input_ + 0.9975f * dc_output_;
    dc_input_ = decimated;
    dc_output_ = dc_out;
    out[i] = dc_out;

    const float dc_aux = decimated_aux - dc_input_aux_ + 0.9975f * dc_output_aux_;
    dc_input_aux_ = decimated_aux;
    dc_output_aux_ = dc_aux;
    aux[i] = dc_aux;
  }

  fir_write_ &= kVowelFirMask;
}

}  // namespace plaits
