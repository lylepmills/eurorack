// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' three noise models -- NOIS, TWNQ and CLKN -- in one slot, on a
// model axis.

#include "plaits/dsp/engine2/noise_bank_engine.h"

#include <algorithm>
#include <cmath>

#include "plaits/build_config.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

inline float NoiseBankFastLog2(float x) {
  union { float f; uint32_t i; } u = { x };
  const float e = static_cast<float>((u.i >> 23) & 0xffu) - 127.0f;
  u.i = (u.i & 0x007fffffu) | 0x3f800000u;
  const float m = u.f;
  return e + (-1.7417939f + (2.8212026f + (-1.4699568f
      + (0.44717955f - 0.056570851f * m) * m) * m) * m);
}

// Braids' ws_moderate_overdrive, verbatim from braids/resources.cc.
//
// Reproduces EXACTLY -- 0 LSB across all 257 entries -- from the closed form
// in braids/resources/waveshapers.py:
//
//   moderate_overdrive = scale(tanh(2 * x))
//   with x = arange(257)/128 - 1, x[-1] = x[-2], and `scale` centring on the
//   mean before normalizing to +/-32766.
//
// checked rather than asserted. Embedded rather than evaluated because it is
// read twice per internal sub-sample, i.e. four times per output sample, and
// a tanh there is not affordable.
const int16_t kOverdrive[257] = {
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
   32728,
};

// Braids' lut_resonator_coefficient, verbatim. Reproduces at 0 LSB (with the
// generator's astype(int) truncation) from braids/resources/lookup_tables.py:
//
//   cutoff = 440 * 2 ** ((arange(129) - 69) / 12)
//   f      = minimum(cutoff / (96000 / 2), 0.25)
//   entry  = 2 * cos(2 * pi * f) * 32768
//
// This one is DATA rather than a closed form for a reason that is not speed.
// Q15 storage of 2*cos(w) runs out of angular resolution as w falls, and the
// error is not small: reading the pole angle back out of the committed table
// puts the resonance +25.9 cents sharp at MIDI 45, +192 cents at MIDI 36, and
// +1641 cents at MIDI 12 -- entries 0..28 are all 65535, so the model has a
// hard 42.20 Hz floor. Computing 2*cos in float instead would put the port in
// tune and out of agreement with the module by up to a semitone.
const uint16_t kResonatorCoefficient[129] = {
   65535,  65535,  65535,  65535,  65535,  65535,  65535,  65535,
   65535,  65535,  65535,  65535,  65535,  65535,  65535,  65535,
   65535,  65535,  65535,  65535,  65535,  65535,  65535,  65535,
   65535,  65535,  65535,  65535,  65535,  65534,  65534,  65534,
   65534,  65534,  65534,  65533,  65533,  65533,  65532,  65532,
   65532,  65531,  65531,  65530,  65529,  65529,  65528,  65527,
   65526,  65525,  65523,  65522,  65520,  65518,  65516,  65514,
   65511,  65508,  65505,  65501,  65497,  65492,  65487,  65481,
   65475,  65467,  65459,  65449,  65439,  65427,  65414,  65399,
   65382,  65363,  65342,  65318,  65292,  65262,  65228,  65191,
   65149,  65101,  65048,  64988,  64922,  64847,  64762,  64668,
   64562,  64443,  64310,  64160,  63992,  63804,  63593,  63356,
   63091,  62794,  62461,  62088,  61670,  61202,  60677,  60091,
   59435,  58701,  57881,  56964,  55941,  54799,  53526,  52107,
   50528,  48773,  46824,  44662,  42268,  39623,  36704,  33492,
   29966,  26107,  21898,  17325,  12380,   7061,   1374,      0,
       0,
};

// Braids' lut_resonator_scale, verbatim, 0 LSB against
//
//   response = sin((n+1) * 2*pi*f) / sin(2*pi*f) * 0.99985**n / (2*f)**0.5
//   entry    = clip(16384 / max|response| over n < 2000, 1, 256)
//
// Embedded because reproducing it means a 2000-term search, and because it is
// an INTEGER that Braids applies as `sample * s >> 16`: at MIDI 45 it is 16,
// so the excitation reaching the resonators has eight distinct levels, and
// below MIDI 32 it is 4 or less, small enough that the shift floors every
// sample to zero and the model is silent. Both are reproduced in integers
// below.
const uint16_t kResonatorScale[129] = {
     1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
     1,    1,    1,    1,    1,    1,    1,    1,    1,    1,
     1,    2,    2,    2,    2,    2,    3,    3,    3,    4,
     4,    4,    5,    5,    6,    6,    7,    8,    8,    9,
    10,   11,   12,   13,   14,   16,   17,   19,   20,   22,
    24,   27,   29,   32,   35,   38,   41,   45,   49,   54,
    58,   64,   70,   76,   83,   90,   98,  107,  117,  128,
   139,  152,  166,  181,  197,  215,  234,  255,  256,  256,
   256,  256,  256,  256,  256,  256,  256,  256,  256,  256,
   256,  256,  256,  256,  256,  256,  256,  256,  256,  256,
   256,  256,  256,  256,  256,  256,  256,  256,  256,  256,
   256,  256,  256,  256,  256,  256,  256,  256,  256,  256,
   256,  256,  256,  256,  256,  256,  256,  256,  256,
};

// Braids reads the shaper with Interpolate88(ws, sample + 32768): the int16
// sample becomes a uint16 index whose top 8 bits select the entry and whose
// low 8 interpolate. `x` here is that input, normalized.
inline float Shaper(float x) {
  float index = (x + 1.0f) * 128.0f;
  CONSTRAIN(index, 0.0f, 255.999f);
  const int integral = static_cast<int>(index);
  const float fractional = index - static_cast<float>(integral);
  const float a = static_cast<float>(kOverdrive[integral]);
  const float b = static_cast<float>(kOverdrive[integral + 1]);
  return (a + (b - a) * fractional) * (1.0f / 32768.0f);
}

// Braids' Interpolate824 over a table indexed by MIDI note: pitch_ << 17
// puts the integer note in the top 8 bits and the 1/128-semitone remainder in
// the next 24, so the read is a linear interpolation between whole notes.
inline float ReadNoteTable(const uint16_t* table, float note) {
  CONSTRAIN(note, 0.0f, 127.999f);
  const int integral = static_cast<int>(note);
  const float fractional = note - static_cast<float>(integral);
  const float a = static_cast<float>(table[integral]);
  const float b = static_cast<float>(table[integral + 1]);
  return a + (b - a) * fractional;
}

// lut_resonator_coefficient, read at the note EXACTLY as Braids reads it, and
// deliberately not retuned onto Plaits' corrected clock. That was tried and
// measured: the table stores 2*cos(w) in Q15, and the recursion consumes it as
// a Q15 integer, so one least-significant bit of the coefficient is a whole
// 116 cents of pole frequency at MIDI 45 and 249 cents at MIDI 36. Scaling the
// pole angle by 48000/kCorrectedSampleRate -- a 4.61-cent correction -- and
// truncating back onto that grid moved the resonance a FULL step instead: the
// spectral delta against the module went from 0.27 dB to 0.60 dB at MIDI 45
// and 1.55 dB at MIDI 36. The grid is coarser than the correction, so the
// correction cannot be applied. TWNQ therefore rings 4.61 cents under the rest
// of the module's engines, which is nothing beside the +25.9 to +1641 cents
// the table's own quantization already puts it out by.
inline uint32_t ResonatorCoefficient(float note) {
  return static_cast<uint32_t>(ReadNoteTable(kResonatorCoefficient, note));
}

// lut_svf_cutoff, as its closed form on Plaits' corrected sample rate:
// f = 2 * sin(pi * cutoff / 96000), the cutoff clamped to an eighth of the
// internal rate. Evaluated at whole notes and interpolated, because that is
// the shape Braids' table read has.
inline float SvfCutoffAt(int note) {
  float f = NoteToFrequency(static_cast<float>(note)) * 0.5f;
  if (f > kNoiseBankMaxCutoff) {
    f = kNoiseBankMaxCutoff;
  }
  return 2.0f * sinf(3.14159265358979f * f);
}

// lut_svf_damp: 2 * (1 - resonance ** 0.25), with resonance = index / 260.
// The generator wraps this in minimum(2, 2/f - f * 0.5); checked over all 257
// entries, that guard NEVER binds -- f tops out at 0.7654 and the guard only
// bites above 0.828 -- so it is not carried.
inline float SvfDampAt(int index) {
  const float resonance = static_cast<float>(index) /
      kNoiseBankResonanceDivisor;
  return 2.0f * (1.0f - Sqrt(Sqrt(resonance)));
}

inline float Lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

// The 19-tap Hamming halfband that takes the 2x internal stream down to the
// output rate. Only the centre and the odd offsets are non-zero, so it is six
// multiplies. Against the 127-tap Blackman-Harris halfband the A/B harness
// decimates the Braids reference with (braids/test/render_braids_model.cc,
// Decimate2x), it is within 0.13 dB to 16 kHz, -1.41 dB at 20 kHz and
// -3.41 dB at 22.4 kHz -- a top-octave difference, which is what the --bands
// breakdown exists to separate from a real one.
inline float Decimate(const float* buffer, uint32_t write) {
#define NB_TAP(j) buffer[(write - 1u - (j)) & kNoiseBankDecimatorMask]
  return 0.49863366f * NB_TAP(9)
      + 0.30863382f * (NB_TAP(8) + NB_TAP(10))
      - 0.08147628f * (NB_TAP(6) + NB_TAP(12))
      + 0.02921222f * (NB_TAP(4) + NB_TAP(14))
      - 0.00850828f * (NB_TAP(2) + NB_TAP(16))
      + 0.00282169f * (NB_TAP(0) + NB_TAP(18));
#undef NB_TAP
}

// Braids works in 15-bit knob counts, and the difference from a float knob is
// not always cosmetic: at the detent a macro of 0.5 is parameter 16383, not
// 16384, and several of these models take an arithmetic shift of
// (parameter - 16384) whose sign flips there. Every parameter below is taken
// through this first, exactly as ToParameter does on the reference side.
inline int32_t Parameter(float macro) {
  int32_t value = static_cast<int32_t>(macro * 32767.0f);
  CONSTRAIN(value, 0, 32767);
  return value;
}

// Braids' noise source: Random::GetSample() >> 1, i.e. the top 16 bits of the
// LCG as a signed sample, halved. Kept as an integer because TWNQ's
// excitation gain is an integer shift whose truncation is audible.
inline int32_t NoiseSample() {
  return static_cast<int32_t>(
      static_cast<int16_t>(Random::GetWord() >> 16)) >> 1;
}

}  // namespace

void NoiseBankEngine::Init(BufferAllocator* allocator) {
  // Two 32-float decimator rings and a handful of filter states; nothing from
  // the shared 16 KiB arena.
  (void) allocator;
  model_quantizer_.Init(NOISE_BANK_MODEL_LAST, 0.05f, false);
  Reset();
}

void NoiseBankEngine::Reset() {
  lp_ = 0.0f;
  bp_ = 0.0f;
  y11_ = y12_ = y21_ = y22_ = 0;
  twin_main_ = 0.0f;
  twin_aux_ = 0.0f;
  clock_phase_ = 0;
  cycle_phase_ = 0;
  // Braids memsets its state, so the sequence starts from zero and the first
  // step of the LCG supplies the first value. The aux stream is offset so the
  // two sides are not the same sequence.
  rng_state_ = 0;
  rng_state_aux_ = 0x9e3779b9;
  seed_ = 0;
  seed_aux_ = 0x9e3779b9;
  clocked_sample_ = 0;
  clocked_sample_aux_ = 0;
  drive_ = 1.0f;
  for (size_t i = 0; i < kNoiseBankDecimatorSize; ++i) {
    decimator_main_[i] = 0.0f;
    decimator_aux_[i] = 0.0f;
  }
  decimator_write_ = 0;
}

void NoiseBankEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  const int model = model_quantizer_.Process(parameters.harmonics);

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // Braids' strike_ handler: a fresh seed for the looping sequence.
    seed_ = Random::GetWord();
    seed_aux_ = Random::GetWord();
    cycle_phase_ = 0;
  }

  float note = parameters.note;
  CONSTRAIN(note, 0.0f, kNoiseBankMaxNote);
  const float base_frequency = NoteToFrequency(note);
  float note_offset[kMaxBlockSize];
  fill(&note_offset[0], &note_offset[size], 0.0f);
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  if (parameters.frequency_offset) {
    for (size_t i = 0; i < size; ++i) {
      const float instantaneous_frequency = max(
          1e-7f, base_frequency + parameters.frequency_offset[i]);
      note_offset[i] = 12.0f * NoiseBankFastLog2(
          instantaneous_frequency / max(base_frequency, 1e-7f));
    }
  }
#endif

  // MACRO drives the shaper. NOIS and TWNQ already end in it at unity, so
  // their stock value is 1.0 and the detent is Braids exactly; CLKN has no
  // shaper at all, so its stock is 0 and the lower half of the knob is inert
  // by construction.
  const bool clocked = model == NOISE_BANK_MODEL_CLOCKED;
  const float target_drive = clocked
      ? ApplyMacro(0.0f, 0.0f, kNoiseBankMaxDrive, parameters.macro)
      : ApplyMacro(1.0f, kNoiseBankMinDrive, kNoiseBankMaxDrive,
                   parameters.macro);
  const float shaper_mix = clocked ? min(1.0f, target_drive) : 1.0f;

  if (model == NOISE_BANK_MODEL_FILTERED) {
    // pitch_ -> lut_svf_cutoff (digital_oscillator.cc:1837).
    const int note_integral = static_cast<int>(note);
    const float base_f = Lerp(SvfCutoffAt(note_integral),
                              SvfCutoffAt(note_integral + 1),
                              note - static_cast<float>(note_integral));

    // parameter_[0] -> lut_svf_damp and lut_svf_scale (:1838, :1839). The
    // table index is parameter_[0] >> 7 with the remainder as the fraction,
    // i.e. the knob count over 128.
    float damp_index = static_cast<float>(Parameter(parameters.timbre)) *
        kNoiseBankDampIndexScale;
    CONSTRAIN(damp_index, 0.0f, 255.999f);
    const int damp_integral = static_cast<int>(damp_index);
    const float damp_fractional =
        damp_index - static_cast<float>(damp_integral);
    const float damp_a = SvfDampAt(damp_integral);
    const float damp_b = SvfDampAt(damp_integral + 1);
    const float damp = Lerp(damp_a, damp_b, damp_fractional);
    const float scale = Lerp(Sqrt(damp_a * 0.5f), Sqrt(damp_b * 0.5f),
                             damp_fractional);

    // :1855. A level normalization, not a rate constant.
    // parameter_[1] -> the LP/BP/HP morph (:1845-1853). Braids works in raw
    // parameter counts, and the crossover is at 16384 of 32767 -- so COLOR at
    // noon is very nearly pure band-pass, not a three-way blend.
    const float p1 = static_cast<float>(Parameter(parameters.morph));
    float lp_gain, bp_gain, hp_gain;
    if (p1 < 16384.0f) {
      bp_gain = p1 * (1.0f / 16384.0f);
      lp_gain = 1.0f - bp_gain;
      hp_gain = 0.0f;
    } else {
      bp_gain = (32767.0f - p1) * (1.0f / 16384.0f);
      hp_gain = (p1 - 16384.0f) * (1.0f / 16384.0f);
      lp_gain = 0.0f;
    }

    ParameterInterpolator drive_modulation(&drive_, target_drive, size);

    for (size_t i = 0; i < size; ++i) {
      const float drive = drive_modulation.Next();
      float f = base_f;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
      if (parameters.frequency_offset) {
        float modulated_note = note + note_offset[i];
        CONSTRAIN(modulated_note, 0.0f, kNoiseBankMaxNote);
        const int modulated_integral = static_cast<int>(modulated_note);
        f = Lerp(
            SvfCutoffAt(modulated_integral),
            SvfCutoffAt(modulated_integral + 1),
            modulated_note - static_cast<float>(modulated_integral));
      }
#endif
      const float gain_correction = f > scale ? scale / f : 1.0f;
      for (int s = 0; s < 2; ++s) {
        const float in = static_cast<float>(NoiseSample()) *
            (1.0f / 32768.0f);

        // Braids' Chamberlin SVF, :1857-1863. `notch` there is in - damp*bp,
        // which is the classic notch output hp + lp; it is taken for AUX.
        const float notch = in - bp_ * damp;
        lp_ += f * bp_;
        CONSTRAIN(lp_, -1.0f, 1.0f);
        const float hp = notch - lp_;
        bp_ += f * hp;

        float result = lp_gain * lp_ + bp_gain * bp_ + hp_gain * hp;
        CONSTRAIN(result, -1.0f, 1.0f);
        float side = notch;
        CONSTRAIN(side, -1.0f, 1.0f);

        result *= gain_correction;
        side *= gain_correction;
        Push(Shaper(result * drive), Shaper(side * drive));
      }
      out[i] = Decimate(decimator_main_, decimator_write_);
      aux[i] = Decimate(decimator_aux_, decimator_write_);
    }
  } else if (model == NOISE_BANK_MODEL_TWIN_PEAKS) {
    // TWNQ runs in INTEGERS, unlike the rest of this engine, and that is a
    // measured decision rather than a stylistic one. Braids' two resonators
    // are int32 recursions whose `>> 15` feedback terms truncate, and the
    // excitation reaching them is `sample * s1 >> 16` with s1 an integer from
    // lut_resonator_scale -- 16 at MIDI 45, so eight excitation levels, and 7
    // at MIDI 36, so three. At those low notes the recursion's own truncation
    // noise is the same order as the drive, and a float port renders a
    // cleaner, narrower resonance: measured +1.15 dB of AC RMS at MIDI 36 with
    // the skirts 1.5 dB down against the module, over an 8-second render where
    // it is systematic and not a windowing artefact. Kept integral, every
    // twin-peaks case in tests/ab.json lands at 0.00 dB.
    const int32_t parameter = Parameter(parameters.timbre);

    // parameter_[0] -> q (:1888) and makeup_gain (:1904).
    const uint32_t q = 65240 + (static_cast<uint32_t>(parameter) >> 7);
    const int32_t q_squared = static_cast<int32_t>(q * q >> 17);
    const int32_t makeup_gain = 8191 - (parameter >> 2);

    // parameter_[1] -> the second peak, +/-64 semitones (:1896). The offset
    // has to be taken in Braids' OWN units -- `(parameter_[1] - 16384) >> 1`
    // over a 1/128-semitone pitch -- and not as a float fraction of the knob,
    // because the arithmetic shift floors and the two are not the same number
    // at the detent: COLOR 0.5 gives parameter_[1] = 16383, so the offset is
    // (-1) >> 1 = -1, one 128th of a semitone BELOW the note, where a float
    // reading lands exactly ON it. That 0.8 cents is inaudible and it is not
    // the point: lut_resonator_scale is an integer table, and 1/128 of a
    // semitone is enough to cross one of its steps. At MIDI 36 it is the
    // difference between s2 = 6 and s2 = 7, which changes how often the
    // 3-level excitation fires at all -- measured as +1.64 dB of AC RMS
    // against the module, with the spectrum shape matching to 0.14 dB.
    const int32_t color = Parameter(parameters.morph);
    float note_2 = note + static_cast<float>((color - 16384) >> 1) *
        (1.0f / 128.0f);
    CONSTRAIN(note_2, 0.0f, 127.999f);

    // The pole coefficients, folded into q exactly as `c1 = c1 * q >> 16`
    // (:1901) does. See ResonatorCoefficient for why they are not corrected
    // onto Plaits' sample rate.
    const int32_t base_c1 = static_cast<int32_t>(
        ResonatorCoefficient(note) * q >> 16);
    const int32_t base_c2 = static_cast<int32_t>(
        ResonatorCoefficient(note_2) * q >> 16);

    // The excitation gain is an integer applied as `sample * s >> 16` (:1910).
    // Below MIDI 32 the table value is 4 or less and the shift floors every
    // sample to zero, so TWNQ is digitally SILENT there -- verified against
    // the module at MIDI 24, where both sides render exact zeros.
    const int32_t base_s1 = static_cast<int32_t>(
        ReadNoteTable(kResonatorScale, note));
    const int32_t base_s2 = static_cast<int32_t>(
        ReadNoteTable(kResonatorScale, note_2));

    ParameterInterpolator drive_modulation(&drive_, target_drive, size);

    for (size_t i = 0; i < size; ++i) {
      const float drive = drive_modulation.Next();
      int32_t c1 = base_c1;
      int32_t c2 = base_c2;
      int32_t s1 = base_s1;
      int32_t s2 = base_s2;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
      if (parameters.frequency_offset) {
        float modulated_note = note + note_offset[i];
        CONSTRAIN(modulated_note, 0.0f, 127.999f);
        float modulated_note_2 = note_2 + note_offset[i];
        CONSTRAIN(modulated_note_2, 0.0f, 127.999f);
        c1 = static_cast<int32_t>(
            ResonatorCoefficient(modulated_note) * q >> 16);
        c2 = static_cast<int32_t>(
            ResonatorCoefficient(modulated_note_2) * q >> 16);
        s1 = static_cast<int32_t>(
            ReadNoteTable(kResonatorScale, modulated_note));
        s2 = static_cast<int32_t>(
            ReadNoteTable(kResonatorScale, modulated_note_2));
      }
#endif

      // RenderTwinPeaksNoise ends in `size -= 2` and writes each sample
      // twice: a 48 kHz algorithm held across two 96 kHz samples. The port
      // updates on the even sub-sample and holds through the odd one, which
      // reproduces the hold and not merely the rate.
      const int32_t n = NoiseSample();
      int32_t y10 = n > 0 ? (n * s1) >> 16 : -(((-n) * s1) >> 16);
      int32_t y20 = n > 0 ? (n * s2) >> 16 : -(((-n) * s2) >> 16);

      y10 += y11_ * c1 >> 15;
      y10 -= y12_ * q_squared >> 15;
      CONSTRAIN(y10, -32767, 32767);
      y12_ = y11_;
      y11_ = y10;

      y20 += y21_ * c2 >> 15;
      y20 -= y22_ * q_squared >> 15;
      CONSTRAIN(y20, -32767, 32767);
      y22_ = y21_;
      y21_ = y20;

      int32_t sum = y10 + y20;
      sum += sum * makeup_gain >> 13;
      CONSTRAIN(sum, -32767, 32767);
      int32_t difference = y10 - y20;
      difference += difference * makeup_gain >> 13;
      CONSTRAIN(difference, -32767, 32767);

      twin_main_ = Shaper(static_cast<float>(sum) * (1.0f / 32768.0f) * drive);
      twin_aux_ = Shaper(
          static_cast<float>(difference) * (1.0f / 32768.0f) * drive);

      Push(twin_main_, twin_aux_);
      Push(twin_main_, twin_aux_);

      out[i] = Decimate(decimator_main_, decimator_write_);
      aux[i] = Decimate(decimator_aux_, decimator_write_);
    }
  } else {
    // The clock: Braids takes the note's phase increment and shifts it left
    // three times, stopping early rather than passing half the sample rate
    // (:1970-1975). Kept in uint32 because the port has to reproduce the snap
    // that follows -- `phase = phase_increment` (:2008) forces the clock onto
    // an exact divisor of the sample rate, and in float the accumulator would
    // stall outright at the long loop lengths below.
    float increment = NoteToFrequency(note) * 0.5f;
    CONSTRAIN(increment, 0.0f, 0.4999f);
    uint32_t base_phase_increment = static_cast<uint32_t>(
        increment * 4294967296.0f);
    for (int i = 0; i < kNoiseBankClockShifts; ++i) {
      if (base_phase_increment < (1UL << 31)) {
        base_phase_increment <<= 1;
      }
    }

    // parameter_[0] -> the LOOP LENGTH of the random sequence, in clock ticks
    // (:1978). ComputePhaseIncrement(p0 - 16384) << 1 is, in normalized
    // terms, the note (p0 - 16384) / 128 read as a frequency against 48 kHz.
    const float cycle_note = static_cast<float>(
        Parameter(parameters.timbre) - 16384) * (1.0f / 128.0f);
    float cycle = NoteToFrequency(cycle_note);
    CONSTRAIN(cycle, 0.0f, 0.4999f);
    const uint32_t cycle_phase_increment = static_cast<uint32_t>(
        cycle * 4294967296.0f);

    // parameter_[1] -> the number of quantization levels (:1982-1986).
    uint32_t num_steps =
        1 + (static_cast<uint32_t>(Parameter(parameters.morph)) >> 10);
    if (num_steps == 1) {
      num_steps = 2;
    }
    const uint32_t quantizer_divider = 65536 / num_steps;

    ParameterInterpolator drive_modulation(&drive_, target_drive, size);

    for (size_t i = 0; i < size; ++i) {
      const float drive = drive_modulation.Next();
      uint32_t phase_increment = base_phase_increment;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
      if (parameters.frequency_offset) {
        float modulated_increment =
            (base_frequency + parameters.frequency_offset[i]) * 0.5f;
        CONSTRAIN(modulated_increment, 0.0f, 0.4999f);
        phase_increment = static_cast<uint32_t>(
            modulated_increment * 4294967296.0f);
        for (int shift = 0; shift < kNoiseBankClockShifts; ++shift) {
          if (phase_increment < (1UL << 31)) {
            phase_increment <<= 1;
          }
        }
      }
#endif
      for (int s = 0; s < 2; ++s) {
        clock_phase_ += phase_increment;
        if (clock_phase_ < phase_increment) {
          rng_state_ = rng_state_ * 1664525u + 1013904223u;
          rng_state_aux_ = rng_state_aux_ * 1664525u + 1013904223u;
          cycle_phase_ += cycle_phase_increment;
          if (cycle_phase_ < cycle_phase_increment) {
            // The sequence repeats: this is what turns CLKN into a pitched
            // tone rather than noise, and it is the thing Plaits' free-running
            // ClockedNoise has no equivalent for.
            rng_state_ = seed_;
            rng_state_aux_ = seed_aux_;
            cycle_phase_ = cycle_phase_increment;
          }
          uint16_t sample = static_cast<uint16_t>(rng_state_);
          sample -= sample % quantizer_divider;
          sample += quantizer_divider >> 1;
          clocked_sample_ = static_cast<int16_t>(sample);

          uint16_t sample_aux = static_cast<uint16_t>(rng_state_aux_);
          sample_aux -= sample_aux % quantizer_divider;
          sample_aux += quantizer_divider >> 1;
          clocked_sample_aux_ = static_cast<int16_t>(sample_aux);

          clock_phase_ = phase_increment;
        }
        const float raw = static_cast<float>(clocked_sample_) *
            (1.0f / 32768.0f);
        const float raw_aux = static_cast<float>(clocked_sample_aux_) *
            (1.0f / 32768.0f);
        Push(raw + (Shaper(raw * drive) - raw) * shaper_mix,
             raw_aux + (Shaper(raw_aux * drive) - raw_aux) * shaper_mix);
      }
      out[i] = Decimate(decimator_main_, decimator_write_);
      aux[i] = Decimate(decimator_aux_, decimator_write_);
    }
  }
}

}  // namespace plaits
