// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// A jet-flute engine derived from Braids' FLUT: a bore and a jet delay line in
// one loop, driven by an asymmetric cubic jet and a breath the noise multiplies.

#include "plaits/dsp/engine2/fluted_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// lut_flute_body_filter, verbatim from braids/resources.cc, first 80 entries.
// Reproduces at 0 LSB across all 128 from its generator in
// braids/resources/lookup_tables.py:171-174,
//
//     int(4096 * min(0.7, 0.4 * 2^((n - 69) / 12)))
//
// checked programmatically against the stored table rather than asserted.
// Rounding instead of truncating misses by 1 LSB, so truncation is the reading
// that reproduces, and the port reads the stored integers rather than the
// closed form: the truncation is proportionally largest exactly where the
// values are smallest (-1.46% at n = 0 against -0.02% at n = 48), and this is
// the only one of the model's three tables whose closed form needs a
// transcendental. Entries 79..127 are all 2867, the min(0.7, ...) ceiling, so
// the index is clamped at 79 here and the tail is not stored.
const uint16_t kFluteBodyFilter[80] = {
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

// 31-tap Kaiser(beta = 6) halfband, 96 -> 48 kHz, the same design blown_engine
// carries. Only the centre tap and the odd offsets are non-zero, so one output
// costs eight multiplies.
//
// R7 -- the residue, measured here rather than inherited from blown, because
// this model is much brighter. Response of these coefficients over the band
// that folds (24..48 kHz): -6.02 dB at 24 kHz (halfband symmetry), -15.5 dB at
// 26.4 kHz, -34.5 dB at 28.8 kHz, below -66 dB from 33 kHz up. So the worst
// alias attenuation is the -15.5 dB just above the fold. What that costs was
// measured by taking this engine's own 96 kHz pre-decimation stream and
// decimating it twice -- once with these coefficients, once with the reference
// renderer's 127-tap Blackman-Harris halfband (whose stopband is below -90 dB).
// Over MIDI 33/45/69/81 crossed with COLOR 0/0.5/1 at TIMBRE 0, the worst case
// for it (6.6% of the native energy sits above 24 kHz at MIDI 45), the 31-tap
// costs 0.02 dB of energy-weighted spectral difference and 0.02 dB of AC RMS,
// all of it in the 20.5-24 kHz band, where its wider transition takes 1.3 dB
// more than the 127-tap does. That band-edge droop, not the aliasing, is the
// accepted residue.
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

// lut_blowing_envelope, evaluated from its generator (lookup_tables.py:150-156)
// rather than stored. The table is two straight lines,
//
//     attack[i] = int(i / 119 * 1.3 * 16384)              i = 0..119
//     decay[i]  = int((1 - 0.2 * i / 239) * 1.3 * 16384)  i = 0..239
//
// and the integer forms below reproduce all 360 reachable entries of
// braids/resources.cc at 0 LSB, checked programmatically. Braids' 32-entry
// guard tail past index 359 holds decay[239] repeated, which is why clamping
// the pointer at 360 -- rather than letting it run into the guard as Braids
// does within a block -- reads identical numbers.
inline int BlowingEnvelope(int index) {
  if (index < kFlutedEnvelopeAttack) {
    return (index * 212992) / 1190;
  }
  int i = index - kFlutedEnvelopeAttack;
  if (i > kFlutedEnvelopeDecay - 1) {
    i = kFlutedEnvelopeDecay - 1;
  }
  return (509050880 - 425984 * i) / 23900;
}

// Braids' lut_blowing_jet verbatim. The former closed-form evaluator produced
// these same 257 integers, but paid for three floating-point multiplies and a
// conversion on every internal sample. Braids reads the table with no
// interpolation, so this is both its original implementation and exactly the
// same 128-step staircase the port already produced.
const int16_t kBlowingJet[257] = {
       0,   -255,   -511,   -767,
   -1022,  -1278,  -1532,  -1786,
   -2039,  -2292,  -2544,  -2795,
   -3044,  -3293,  -3541,  -3787,
   -4031,  -4275,  -4516,  -4756,
   -4994,  -5231,  -5465,  -5697,
   -5927,  -6155,  -6381,  -6604,
   -6824,  -7042,  -7257,  -7470,
   -7679,  -7886,  -8089,  -8289,
   -8486,  -8680,  -8870,  -9056,
   -9239,  -9418,  -9594,  -9765,
   -9932, -10095, -10254, -10409,
  -10559, -10705, -10846, -10982,
  -11114, -11241, -11363, -11480,
  -11591, -11698, -11799, -11894,
  -11984, -12069, -12147, -12220,
  -12287, -12348, -12403, -12452,
  -12494, -12530, -12560, -12583,
  -12599, -12609, -12611, -12607,
  -12596, -12578, -12552, -12519,
  -12479, -12431, -12376, -12313,
  -12242, -12163, -12077, -11982,
  -11879, -11768, -11649, -11521,
  -11384, -11239, -11085, -10923,
  -10751, -10571, -10381, -10182,
   -9974,  -9757,  -9530,  -9293,
   -9047,  -8791,  -8526,  -8250,
   -7964,  -7668,  -7362,  -7046,
   -6719,  -6382,  -6034,  -5676,
   -5306,  -4926,  -4535,  -4133,
   -3719,  -3295,  -2859,  -2411,
   -1952,  -1482,  -1000,   -506,
       0,    517,   1048,   1590,
    2144,   2711,   3291,   3883,
    4487,   5105,   5735,   6378,
    7034,   7704,   8386,   9082,
    9791,  10514,  11250,  12000,
   12764,  13542,  14333,  15139,
   15959,  16793,  17642,  18504,
   19382,  20274,  21181,  22102,
   23039,  23990,  24957,  25939,
   26936,  27948,  28976,  30019,
   31079,  32153,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,  32767,  32767,  32767,
   32767,
};

inline int BlowingJet(int index) {
  return kBlowingJet[index >> 8];
}

// stmlib::Random's generator, so the breath noise has Braids' distribution
// (Braids reads Random::GetSample(), the top 16 bits of this LCG as an int16).
// The port carries its own state rather than sharing the global one.
inline int NextNoise(uint32_t* state) {
  *state = *state * 1664525u + 1013904223u;
  return static_cast<int>(static_cast<int16_t>(*state >> 16));
}

// stmlib::Mix(a, b, balance) = `(a * (65535 - balance) + b * balance) >> 16`
// (stmlib/utils/dsp.h:86), which is how Braids reads its delay lines at a
// fractional delay (digital_oscillator.cc:1412-1417).
//
// Worth spelling out, because a float transcription is NOT equivalent: the
// arguments are int8 line entries, so the interpolated value is floored back
// ONTO THE INT8 GRID, and at balance 0 the read does not even return `a`
// (127 comes back as 126). Braids' fractional delay biases WHICH INTEGER
// comes out; it does not produce a fractional sample value. Measured: with
// this read done in float instead, the engine sat 0.09 dB above the module in
// AC RMS at MIDI 33 (held, TIMBRE 1.0); reproducing the floor took that to
// 0.00 dB. The arithmetic is reproduced, not approximated.
inline int MixLine(
    const int8_t* line, uint32_t mask, uint32_t read, uint32_t balance) {
  const int a = static_cast<int>(line[read & mask]);
  const int b = static_cast<int>(line[(read - 1) & mask]);
  return (a * static_cast<int>(65535u - balance) +
          b * static_cast<int>(balance)) >> 16;
}

// Braids' `lut_flute_body_filter[pitch_ >> 7]` (1404) with the out-of-bounds
// read fixed: pitch_ is clamped only to 140 * 128 by Render()
// (digital_oscillator.cc:44, 120-124) while the table holds 128 entries, and
// RenderBlown reads the same table off its own `pitch_ >> 7` -- a different
// index, but the same arithmetic -- and clamps THAT to 0..127 (1325-1329),
// which is what proves the intent. Every entry from 79 up is the table's
// ceiling, so the clamp cannot change a note the module actually defines.
inline int BodyCoefficient(float note) {
  int pitch = static_cast<int>(note * 128.0f);
  CONSTRAIN(pitch, 0, 140 * 128);
  int index = pitch >> 7;
  CONSTRAIN(index, 0, 79);
  return static_cast<int>(kFluteBodyFilter[index]);
}

}  // namespace

void FlutedEngine::Init(BufferAllocator* allocator) {
  bore_ = allocator->Allocate<int8_t>(kFlutedBoreLength);
  jet_ = allocator->Allocate<int8_t>(kFlutedJetLength);
  Reset();
}

void FlutedEngine::Reset() {
  // DigitalOscillator::Init() zeroes state_ and Render() calls it on every
  // shape change (digital_oscillator.cc:111-115). delay_lines_ is NOT in that
  // union, but on the module the oscillator is a file-scope object
  // (braids.cc:59) and so zero-initialized, and Strike() clears both lines
  // anyway (1381-1387). So Braids' FLUT always starts from silence and there
  // is no resting state to reproduce. The arena is shared, so the lines have
  // to be cleared on every engine switch and not only at Init (SPEC R15).
  if (bore_) {
    for (size_t i = 0; i < kFlutedBoreLength; ++i) {
      bore_[i] = 0;
    }
  }
  if (jet_) {
    for (size_t i = 0; i < kFlutedJetLength; ++i) {
      jet_[i] = 0;
    }
  }
  delay_pointer_ = 0;
  lp_state_ = 0;
  dc_blocking_x0_ = 0;
  dc_blocking_y0_ = 0;
  excitation_pointer_ = 0;
  excitation_divider_ = 0;
  noise_depth_ = 2100.0f;
  blow_ = kFlutedBlowStock;
  rng_state_ = 0x4b1d07a3u;
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

void FlutedEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (!bore_ || !jet_) {
    // Allocate<T> returns NULL silently when the arena is exhausted (R15).
    for (size_t i = 0; i < size; ++i) {
      out[i] = 0.0f;
      aux[i] = 0.0f;
    }
    return;
  }

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // Braids' strike_ handling, 1381-1387: restart the excitation envelope,
    // flush both lines, clear the reflection one-pole. The delay pointer and
    // the DC blocker's two states are deliberately NOT cleared -- carrying
    // them across a strike is what the module does.
    for (size_t i = 0; i < kFlutedBoreLength; ++i) {
      bore_[i] = 0;
    }
    for (size_t i = 0; i < kFlutedJetLength; ++i) {
      jet_[i] = 0;
    }
    lp_state_ = 0;
    excitation_pointer_ = 0;
    excitation_divider_ = 0;
  }

  // R5: the internal rate is 96 kHz, two steps per output sample, so Braids'
  // delay arithmetic transfers as written. Braids' `delay_` is the period in
  // 96 kHz samples as 16.16 fixed point (ComputeDelay, digital_oscillator.cc:
  // 73-92, over a table generated as `96000 / f * 65536 * 4096`), and
  // `frequency` here is cycles per OUTPUT sample, so that period is
  // 2 / frequency internal samples. Checked against the module's own table:
  // the two agree to 0.0002 cents at every octave, the whole difference being
  // the 4.61 cents kCorrectedSampleRate costs every engine in this port (R6).
  //
  // ComputeDelay clamps its pitch at kHighestNote - kOctave = MIDI 128
  // (75-77), so the bore stops shortening there; the port clamps the same way.
  float delay_note = parameters.note;
  CONSTRAIN(delay_note, 0.0f, 128.0f);
  const float frequency = max(1e-6f, NoteToFrequency(delay_note));
  const uint32_t delay_fixed = static_cast<uint32_t>(
      min(2.0f / frequency, 16383.0f) * 65536.0f);

  // Lines 1390-1397, integer for integer: one loop length, split by COLOR,
  // then folded up an octave at a time until both halves fit their lines.
  uint32_t bore_delay = (delay_fixed << 1) - (2u << 16);
  const int color_code = static_cast<int>(parameters.harmonics * 32767.0f);
  uint32_t jet_delay = (bore_delay >> 8) *
      static_cast<uint32_t>(48 + (color_code >> 10));
  bore_delay -= jet_delay;
  int guard = 0;
  while (guard < 24 &&
         (bore_delay > ((kFlutedBoreLength - 1) << 16) ||
          jet_delay > ((kFlutedJetLength - 1) << 16))) {
    bore_delay >>= 1;
    jet_delay >>= 1;
    ++guard;
  }
  const uint32_t bore_delay_integral = bore_delay >> 16;
  const uint32_t bore_delay_fractional = bore_delay & 0xffff;
  const uint32_t jet_delay_integral = jet_delay >> 16;
  const uint32_t jet_delay_fractional = jet_delay & 0xffff;

  // The second bore tap AUX is read from, mono and stereo alike.
  uint32_t pickup = static_cast<uint32_t>(
      static_cast<float>(bore_delay) * kFlutedAuxPickup);
  if (pickup < (2u << 16)) {
    pickup = 2u << 16;
  }
  const uint32_t pickup_integral = pickup >> 16;
  const uint32_t pickup_fractional = pickup & 0xffff;

  // Line 1403: breath_intensity = 2100 - (TIMBRE >> 4), applied at 1421 as a
  // fraction of 4096.
  const int timbre_code = static_cast<int>(parameters.timbre * 32767.0f);
  const float target_noise_depth =
      2100.0f - static_cast<float>(timbre_code >> 4);

  // The two macros Braids does not have, both stock at noon. ApplyMacro
  // returns the stock value EXACTLY at 0.5, so blow_code is 4096 and the Body
  // offset is 0 there, and the loop below reduces to Braids' arithmetic term
  // for term.
  const float target_blow = ApplyMacro(
      kFlutedBlowStock, kFlutedBlowMin, kFlutedBlowMax, parameters.morph);
  const float body_offset = ApplyMacro(
      kFlutedBodyStock, kFlutedBodyMin, kFlutedBodyMax, parameters.macro);

  // The reflection filter's corner. Braids derives it from the note alone and
  // gives it no knob (1404); Body offsets that index.
  const int body_coefficient = BodyCoefficient(parameters.note + body_offset);
  const int body_complement = 4096 - body_coefficient;

  ParameterInterpolator noise_modulation(
      &noise_depth_, target_noise_depth, size);
  ParameterInterpolator blow_modulation(&blow_, target_blow, size);

  const uint32_t bore_mask = kFlutedBoreLength - 1;
  const uint32_t jet_mask = kFlutedJetLength - 1;

  while (size--) {
    // ROUND, do not truncate. Braids' `breath_intensity` is an exact integer
    // (2100 - (TIMBRE >> 4)); a ParameterInterpolator only ever approaches its
    // target -- `value_ += increment_` with no snap (parameter_interpolator.h:
    // 59-62) -- so a settled ramp lands a float epsilon BELOW the integer and
    // truncation then sits 1 LSB low forever. Measured before this rounding:
    // TIMBRE 1.0 settled at 52 where Braids reads 53, and TIMBRE 0.9 at 256
    // where Braids reads 257. Both values are positive, so `+ 0.5f` is a plain
    // round. Same reasoning for blow_code, which must be exactly 4096 at noon
    // for the loop to reduce to Braids' arithmetic after MORPH has moved.
    const int breath_intensity =
        static_cast<int>(noise_modulation.Next() + 0.5f);
    const int blow_code =
        static_cast<int>(blow_modulation.Next() * 4096.0f + 0.5f);

    for (int j = 0; j < 2; ++j) {
      // The two fractional reads, 1408-1417. `<< 9` puts an int8 line entry
      // back into the 32768 == 1.0 scale the rest of the loop works in.
      const int32_t bore_value = MixLine(
          bore_, bore_mask,
          static_cast<uint32_t>(
              delay_pointer_ + 2 * kFlutedBoreLength - bore_delay_integral),
          bore_delay_fractional) << 9;
      const int32_t jet_value = MixLine(
          jet_, jet_mask,
          static_cast<uint32_t>(
              delay_pointer_ + 2 * kFlutedJetLength - jet_delay_integral),
          jet_delay_fractional) << 9;

      // The breath: the excitation envelope doubled (1419-1420), with
      // MULTIPLICATIVE noise (1421-1423). Blow scales what Braids welds; at
      // noon blow_code is 4096 and the pair of shifts is the identity.
      int32_t breath_pressure = BlowingEnvelope(excitation_pointer_) << 1;
      breath_pressure = breath_pressure * blow_code >> 12;
      int32_t random_pressure =
          NextNoise(&rng_state_) * breath_intensity >> 12;
      random_pressure = random_pressure * breath_pressure >> 15;
      breath_pressure += random_pressure;

      // The inverting reflection one-pole, 1425-1426.
      lp_state_ = (-body_coefficient * bore_value +
                   body_complement * lp_state_) >> 12;

      // The in-loop DC blocker, 1428-1431.
      dc_blocking_y0_ = kFlutedDcBlockingPole * dc_blocking_y0_ >> 12;
      dc_blocking_y0_ += lp_state_ - dc_blocking_x0_;
      dc_blocking_x0_ = lp_state_;
      const int32_t reflection = dc_blocking_y0_;

      // The jet line takes the pressure difference at the embouchure, 1433.
      jet_[delay_pointer_ & jet_mask] =
          static_cast<int8_t>((breath_pressure - (reflection >> 1)) >> 9);

      // The jet characteristic, 1436-1445: the flow is clamped to 0..65535 --
      // a cut at the bottom that the module's own operating point almost never
      // reaches (see the header), a saturation at the top -- and then read
      // through the quantised cubic.
      int32_t jet_table_index = jet_value;
      CONSTRAIN(jet_table_index, 0, 65535);
      const int32_t bore_input = BlowingJet(jet_table_index) + (reflection >> 1);

      // A second tap halfway along the bore, read before this sample's write
      // lands. Not in Braids -- it is what AUX carries, and in stereo it is
      // the right channel.
      const int32_t pickup_value = MixLine(
          bore_, bore_mask,
          static_cast<uint32_t>(
              delay_pointer_ + 2 * kFlutedBoreLength - pickup_integral),
          pickup_fractional) << 9;

      bore_[delay_pointer_ & bore_mask] = static_cast<int8_t>(bore_input >> 9);
      ++delay_pointer_;

      // Braids advances the excitation on three samples in four, because the
      // guard is `if (size & 3)` over a block counting down (1452-1454), and
      // clamps the pointer to LUT_BLOWING_ENVELOPE_SIZE - 32 (1456-1458).
      if ((excitation_divider_ & 3u) != 3u &&
          excitation_pointer_ < kFlutedEnvelopePlateau) {
        ++excitation_pointer_;
      }
      ++excitation_divider_;

      // The module's output, 1449-1451.
      int32_t sample = bore_value >> 1;
      CLIP(sample)

      int32_t aux_sample = pickup_value >> 1;
      CLIP(aux_sample)

      history_[history_write_ & 31] =
          static_cast<float>(sample) * (1.0f / 32768.0f);
      history_aux_[history_write_ & 31] =
          static_cast<float>(aux_sample) * (1.0f / 32768.0f);
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

    // The jet characteristic is read over a positive-biased range, so the bore
    // parks well off zero -- the reference measures +0.095 to +0.153 by note,
    // which Braids emits and a Plaits engine must not. R1 then applies: an
    // engine with a DC blocker cannot pin its post-blocker peak, so this
    // engine registers negative gains and takes the limiter path.
    ONE_POLE(out_dc_input_, decimated, kFlutedDcCoefficient);
    ONE_POLE(aux_dc_input_, decimated_aux, kFlutedDcCoefficient);
    out_dc_output_ = decimated - out_dc_input_;
    aux_dc_output_ = decimated_aux - aux_dc_input_;

    *out++ = out_dc_output_;
    *aux++ = aux_dc_output_;
  }
}

}  // namespace plaits
