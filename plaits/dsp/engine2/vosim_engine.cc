// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' VOSM: two sine formants under a bell window restarted by the note.

#include "plaits/dsp/engine2/vosim_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' lut_bell, verbatim from braids/resources.cc:1006.
//
// It reproduces EXACTLY -- 0 LSB of deviation across all 257 entries -- from
// its generator in braids/resources/lookup_tables.py:239-247,
//
//   n = 256 / 16
//   first_half  = numpy.hanning(n * 2)[:n]   * 65535
//   second_half = numpy.hanning(240 * 2)[240:] * 65535
//   bell        = first_half + second_half + [0]
//
// under TRUNCATION toward zero, not rounding: rounding leaves 124 entries a
// count high. That is checked rather than asserted, and the truncated integers
// are what is embedded, because the truncated integers are what Braids reads.
//
// The data is embedded rather than evaluated because the closed form is a
// cosine per sub-sample AND because the shape is NOT the closed form. Sixteen
// entries of attack read with linear interpolation depart from the Hann segment
// they sample by up to 167 LSB -- 0.255 % of the window's height, worst at
// index 0.5, right on the grain's leading edge. That polygon is the window
// Braids applies, so it is what the port applies.
const uint16_t kBell[257] = {
       0,    670,   2655,   5873,  10191,  15434,  21387,  27805,
   34427,  40980,  47198,  52824,  57630,  61417,  64032,  65366,
   65534,  65528,  65517,  65500,  65477,  65449,  65415,  65376,
   65331,  65280,  65224,  65162,  65095,  65022,  64944,  64860,
   64770,  64675,  64574,  64468,  64357,  64240,  64118,  63990,
   63857,  63718,  63575,  63426,  63271,  63112,  62947,  62777,
   62602,  62421,  62236,  62046,  61850,  61650,  61444,  61234,
   61018,  60798,  60573,  60343,  60109,  59870,  59626,  59377,
   59124,  58866,  58604,  58338,  58067,  57791,  57512,  57228,
   56940,  56648,  56351,  56051,  55746,  55438,  55126,  54810,
   54490,  54166,  53839,  53508,  53173,  52835,  52494,  52149,
   51801,  51449,  51094,  50736,  50375,  50011,  49645,  49275,
   48902,  48526,  48148,  47767,  47384,  46998,  46610,  46219,
   45826,  45431,  45033,  44633,  44232,  43828,  43423,  43015,
   42606,  42195,  41783,  41369,  40953,  40537,  40118,  39699,
   39278,  38856,  38433,  38010,  37585,  37159,  36733,  36306,
   35879,  35450,  35022,  34593,  34163,  33734,  33304,  32874,
   32445,  32015,  31585,  31156,  30727,  30298,  29869,  29442,
   29014,  28588,  28162,  27737,  27312,  26889,  26467,  26045,
   25625,  25206,  24789,  24373,  23958,  23545,  23133,  22723,
   22315,  21908,  21504,  21101,  20700,  20302,  19905,  19511,
   19119,  18730,  18343,  17958,  17576,  17196,  16819,  16445,
   16074,  15706,  15340,  14978,  14618,  14262,  13909,  13559,
   13212,  12869,  12529,  12193,  11860,  11531,  11206,  10884,
   10566,  10252,   9941,   9635,   9332,   9034,   8740,   8450,
    8164,   7882,   7604,   7331,   7062,   6798,   6538,   6283,
    6032,   5786,   5544,   5307,   5075,   4848,   4625,   4407,
    4195,   3987,   3784,   3586,   3393,   3205,   3022,   2844,
    2671,   2504,   2342,   2185,   2033,   1887,   1746,   1610,
    1479,   1354,   1235,   1121,   1012,    909,    811,    719,
     632,    550,    475,    405,    340,    281,    228,    180,
     138,    101,     70,     45,     25,     11,      2,      0,
       0
};

// Braids reads the window with Interpolate824(lut_bell, phase_): the top eight
// bits of the phase select the entry and the next sixteen interpolate toward
// the next one. Braids then does `>> 1` before a `* sample >> 15`, so the
// effective scale is the table value over 65536 -- which is where the /65536
// here comes from, and why the window tops out at 65534/65536 rather than 1.
inline float Bell(float phase) {
  float index = phase * 256.0f;
  CONSTRAIN(index, 0.0f, 255.999f);
  const int integral = static_cast<int>(index);
  const float fractional = index - static_cast<float>(integral);
  const float a = static_cast<float>(kBell[integral]);
  const float b = static_cast<float>(kBell[integral + 1]);
  return (a + (b - a) * fractional) * (1.0f / 65536.0f);
}

// Braids' knob -> formant pitch: `ComputePhaseIncrement(parameter_ >> 1)` on a
// 15-bit knob, so the pitch lands on a 1/128-semitone grid. `>> 1` on a knob
// that stops at 32767 tops out at 16383, one count below the kPitchTableStart
// clamp inside ComputePhaseIncrement, so the ceiling is MIDI 127.9922 and NO
// separate clamp is needed here -- writing one would be the dead guard SPEC R4
// calls out. The quantisation is reproduced because it is free; it is 0.008 of
// a semitone, so it is not what makes or breaks the A/B.
inline float FormantNote(float knob) {
  int parameter = static_cast<int>(knob * 32767.0f);
  CONSTRAIN(parameter, 0, 32767);
  return static_cast<float>(parameter >> 1) * (1.0f / 128.0f);
}

// The window's attack fraction, as a piecewise-linear warp of the carrier
// phase feeding the table. At the stock breakpoint both slopes are exactly 1,
// so MORPH at noon reads lut_bell unwarped and the engine is Braids.
inline float WarpPhase(float phase, float breakpoint,
                       float attack_slope, float decay_slope) {
  return phase < breakpoint
      ? phase * attack_slope
      : kVosimStockBreakpoint + (phase - breakpoint) * decay_slope;
}

}  // namespace

// Symmetric 15-tap halfband read out of a 16-entry ring buffer. The caller has
// already advanced the position, so `p` is the newest sub-sample and the centre
// tap sits seven back. The position advances by exactly two between reads, so
// the decimation phase is constant.
inline float VosimEngine::Decimate(const float* history) const {
  const int p = history_position_;
  return kVosimHalfbandCentre * history[(p - 7) & 15]
      + kVosimHalfband1 * (history[(p - 6) & 15] + history[(p - 8) & 15])
      + kVosimHalfband3 * (history[(p - 4) & 15] + history[(p - 10) & 15])
      + kVosimHalfband5 * (history[(p - 2) & 15] + history[(p - 12) & 15])
      + kVosimHalfband7 * (history[p] + history[(p - 14) & 15]);
}

void VosimEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void VosimEngine::Reset() {
  // DigitalOscillator::Init (digital_oscillator.h:246-258) memsets state_ and
  // zeroes phase_, and Render calls it on every shape change
  // (digital_oscillator.cc:111-115) -- so a Braids VOSM always starts from all
  // phases at zero, and so does this. The formant phases are stored carrying
  // the -cos offset, so Braids' zero is 0.75 here.
  phase_ = 0.0f;
  formant_phase_[0] = 0.75f;
  formant_phase_[1] = 0.75f;
  for (int i = 0; i < 16; ++i) {
    history_[i] = 0.0f;
    history_aux_[i] = 0.0f;
  }
  history_position_ = 0;
  // The real seed needs the note and both formant pitches, which arrive with
  // the first block; see the kVosimSeedSteps integral in Render().
  dc_input_ = 0.0f;
  dc_input_aux_ = 0.0f;
  dc_seeded_ = false;
}

void VosimEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // Braids' sync input zeroes the carrier phase (digital_oscillator.cc:419-421),
    // which on the next sample trips the once-per-cycle reset and restarts both
    // formants; doing both here reaches the same state one sub-sample earlier.
    phase_ = 0.0f;
    formant_phase_[0] = 0.75f;
    formant_phase_[1] = 0.75f;
  }

  const bool stereo = PLAITS_STEREO_VOSIM && parameters.stereo;

  // Braids recomputes all three increments once per block of 24 at 96 kHz
  // (digital_oscillator.cc:117 and :414-416). kBlockSize at 48 kHz with 2x
  // oversampling is the same 4 kHz update rate, so this stays per-block and
  // no parameter interpolator is needed.
  const float increment = NoteToFrequency(parameters.note) * 0.5f;
  const float formant_increment_1 =
      NoteToFrequency(FormantNote(parameters.timbre)) * 0.5f;
  const float formant_increment_2 =
      NoteToFrequency(FormantNote(parameters.harmonics)) * 0.5f;

  // MORPH: the bell's attack fraction, Braids' 1/16 at noon.
  const float breakpoint = ApplyMacro(
      kVosimStockBreakpoint, kVosimMinBreakpoint, kVosimMaxBreakpoint,
      parameters.morph);
  const float attack_slope = kVosimStockBreakpoint / breakpoint;
  const float decay_slope =
      (1.0f - kVosimStockBreakpoint) / (1.0f - breakpoint);

  // MACRO: the share of the pedestal that goes to formant 1, Braids' 2/3 --
  // the 2:1 balance -- at noon. The two amplitudes always sum to the same
  // value, so the grain stays unipolar and the window never opens onto a step.
  const float share = ApplyMacro(kVosimStockShare, 0.0f, 1.0f, parameters.macro);
  float share_main = share;
  float share_aux = 1.0f - share;
  if (stereo) {
    share_main = share - kVosimStereoBalance;
    share_aux = share + kVosimStereoBalance;
    CONSTRAIN(share_main, 0.0f, 1.0f);
    CONSTRAIN(share_aux, 0.0f, 1.0f);
  }

  // wav_sine's own offset and its 0.4 %-short amplitude fold into the pedestal
  // and the two gains here, so the inner loop pays nothing for them.
  const float pedestal = kVosimPedestal * (1.0f + kVosimSineOffset);
  const float a1_main = kVosimSineAmplitude * kVosimPedestal * share_main;
  const float a2_main = kVosimSineAmplitude * kVosimPedestal - a1_main;
  const float a1_aux = kVosimSineAmplitude * kVosimPedestal * share_aux;
  const float a2_aux = kVosimSineAmplitude * kVosimPedestal - a1_aux;

  if (!dc_seeded_) {
    // One grain, integrated once, to put both blockers on the model's offset
    // before the first sample rather than 100 ms after it. Both formants
    // restart with the carrier, so their phase at any point in the grain is a
    // fixed function of the carrier phase -- the same pairing the loop below
    // uses -- and the window sees the same stretch of each sine every cycle.
    // That is why the pedestal term alone is not the offset; see the header.
    const float step = 1.0f / static_cast<float>(kVosimSeedSteps);
    const float ratio_1 = formant_increment_1 / increment;
    const float ratio_2 = formant_increment_2 / increment;
    const bool integrate_1 = ratio_1 < kVosimSeedMaxRatio;
    const bool integrate_2 = ratio_2 < kVosimSeedMaxRatio;
    const float delta_1 = integrate_1 ? ratio_1 * step : 0.0f;
    const float delta_2 = integrate_2 ? ratio_2 * step : 0.0f;
    float seed_phase_1 = 0.75f;
    float seed_phase_2 = 0.75f;
    float sum_window = 0.0f;
    float sum_formant_1 = 0.0f;
    float sum_formant_2 = 0.0f;
    for (int k = 0; k < kVosimSeedSteps; ++k) {
      const float carrier = static_cast<float>(k) * step;
      const float window = Bell(
          WarpPhase(carrier, breakpoint, attack_slope, decay_slope));
      sum_window += window;
      if (integrate_1) {
        sum_formant_1 += window * SineNoWrap(seed_phase_1);
        seed_phase_1 += delta_1;
        if (seed_phase_1 >= 1.0f) {
          seed_phase_1 -= 1.0f;
        }
      }
      if (integrate_2) {
        sum_formant_2 += window * SineNoWrap(seed_phase_2);
        seed_phase_2 += delta_2;
        if (seed_phase_2 >= 1.0f) {
          seed_phase_2 -= 1.0f;
        }
      }
    }
    dc_input_ = (pedestal * sum_window
        + a1_main * sum_formant_1 + a2_main * sum_formant_2) * step
        - kVosimPedestal;
    dc_input_aux_ = (pedestal * sum_window
        + a1_aux * sum_formant_1 + a2_aux * sum_formant_2) * step
        - kVosimPedestal;
    dc_seeded_ = true;
  }

  for (size_t i = 0; i < size; ++i) {
    for (int s = 0; s < 2; ++s) {
      phase_ += increment;
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      }
      formant_phase_[0] += formant_increment_1;
      if (formant_phase_[0] >= 1.0f) {
        formant_phase_[0] -= 1.0f;
      }
      formant_phase_[1] += formant_increment_2;
      if (formant_phase_[1] >= 1.0f) {
        formant_phase_[1] -= 1.0f;
      }

      const float f1 = SineNoWrap(formant_phase_[0]);
      const float f2 = SineNoWrap(formant_phase_[1]);
      const float window = Bell(
          WarpPhase(phase_, breakpoint, attack_slope, decay_slope));

      float sample = (pedestal + a1_main * f1 + a2_main * f2) * window;
      float sample_aux = (pedestal + a1_aux * f1 + a2_aux * f2) * window;

      // digital_oscillator.cc:430-434: at the top of every carrier cycle both
      // formants restart and the sample is forced to zero, which after the
      // pedestal subtraction below is the output's floor at -0.75. The window
      // is already ~0 there, so the forced value changes almost nothing; the
      // phase reset is the whole point of the model.
      if (phase_ < increment) {
        formant_phase_[0] = 0.75f;
        formant_phase_[1] = 0.75f;
        sample = 0.0f;
        sample_aux = 0.0f;
      }

      history_position_ = (history_position_ + 1) & 15;
      history_[history_position_] = sample - kVosimPedestal;
      history_aux_[history_position_] = sample_aux - kVosimPedestal;
    }

    // Braids leaves the -0.375 FS offset in; Plaits' output is DC-coupled, so
    // it comes out here. The blocker was SEEDED at that offset in Reset(), so
    // this does not open with a 100 ms sweep. R1: the post-blocker peak cannot
    // be pinned, hence the negative gains in the manifest.
    const float raw = Decimate(history_);
    const float raw_aux = Decimate(history_aux_);
    ONE_POLE(dc_input_, raw, 0.001f);
    ONE_POLE(dc_input_aux_, raw_aux, 0.001f);
    out[i] = raw - dc_input_;
    aux[i] = raw_aux - dc_input_aux_;
  }
}

}  // namespace plaits
