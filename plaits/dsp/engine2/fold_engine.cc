// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' FOLD: a sine wavefolder and a triangle wavefolder, crossfaded.

#include "plaits/dsp/engine2/fold_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/downsampler/4x_downsampler.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' ws_sine_fold and ws_tri_fold, verbatim from braids/resources.cc.
//
// Both reproduce EXACTLY -- 0 LSB of deviation across all 257 entries -- from
// the closed forms in braids/resources/waveshapers.py:
//
//   tri_fold  = scale(sin(pi * (3x + (2x)^3)),                       center=True)
//   sine_fold = scale(sin(8*pi*x) * w + atan(3x) * (1 - w),          center=False)
//               with w = exp(-4x^2)^1.5, normalized by its own peak
//
// which is checked rather than asserted, following the practice bowed set. The
// data is embedded rather than evaluated because the closed forms cost a sin
// plus an exp plus an atan per lookup, and the folders are read eight times
// per output sample at 4x oversampling.
//
// Note the asymmetric ranges: sine_fold spans -32766..32682 and tri_fold
// -32766..32610, because `scale` normalizes on the larger side. Braids leaves
// that asymmetry in the signal, so the port does too.

const int16_t kSineFold[257] = {
  -32766, -32682, -32595, -32504, -32410, -32315, -32218, -32121,
  -32025, -31931, -31840, -31754, -31673, -31599, -31531, -31470,
  -31416, -31367, -31322, -31279, -31235, -31188, -31132, -31064,
  -30980, -30874, -30744, -30584, -30392, -30166, -29903, -29605,
  -29273, -28911, -28524, -28119, -27704, -27290, -26888, -26510,
  -26167, -25871, -25633, -25461, -25361, -25337, -25389, -25511,
  -25696, -25931, -26197, -26475, -26739, -26962, -27116, -27171,
  -27099, -26876, -26478, -25891, -25105, -24119, -22940, -21584,
  -20079, -18458, -16766, -15052, -13374, -11789, -10357,  -9135,
   -8176,  -7526,  -7217,  -7272,  -7698,  -8484,  -9603, -11013,
  -12652, -14446, -16308, -18142, -19847, -21320, -22465, -23190,
  -23420, -23094, -22176, -20652, -18533, -15860, -12697,  -9137,
   -5292,  -1294,   2713,   6576,  10143,  13266,  15812,  17665,
   18734,  18960,  18316,  16811,  14491,  11437,   7763,   3614,
    -844,  -5428,  -9944, -14200, -18011, -21206, -23640, -25199,
  -25802, -25410, -24026, -21695, -18501, -14568, -10051,  -5129,
       0,   5129,  10051,  14568,  18501,  21695,  24026,  25410,
   25802,  25199,  23640,  21206,  18011,  14200,   9944,   5428,
     844,  -3614,  -7763, -11437, -14491, -16811, -18316, -18960,
  -18734, -17665, -15812, -13266, -10143,  -6576,  -2713,   1294,
    5292,   9137,  12697,  15860,  18533,  20652,  22176,  23094,
   23420,  23190,  22465,  21320,  19847,  18142,  16308,  14446,
   12652,  11013,   9603,   8484,   7698,   7272,   7217,   7526,
    8176,   9135,  10357,  11789,  13374,  15052,  16766,  18458,
   20079,  21584,  22940,  24119,  25105,  25891,  26478,  26876,
   27099,  27171,  27116,  26962,  26739,  26475,  26197,  25931,
   25696,  25511,  25389,  25337,  25361,  25461,  25633,  25871,
   26167,  26510,  26888,  27290,  27704,  28119,  28524,  28911,
   29273,  29605,  29903,  30166,  30392,  30584,  30744,  30874,
   30980,  31064,  31132,  31188,  31235,  31279,  31322,  31367,
   31416,  31470,  31531,  31599,  31673,  31754,  31840,  31931,
   32025,  32121,  32218,  32315,  32410,  32504,  32595,  32682,
   32682
};

const int16_t kTriFold[257] = {
     -78, -20070, -31636, -30481, -17545,   1825,  20257,  31198,
   31144,  20555,   3335, -14748, -28051, -32765, -27869, -15160,
    1526,  17553,  28787,  32606,  28401,  17522,   2767, -12404,
  -24698, -31675, -32164, -26370, -15686,  -2305,  11261,  22670,
   30122,  32609,  29985,  22895,  12593,    688, -11123, -21293,
  -28618, -32346, -32225, -28466, -21675, -12730,  -2661,   7481,
   16727,  24281,  29569,  32266,  32296,  29804,  25121,  18711,
   11123,   2936,  -5286, -13027, -19851, -25419, -29496, -31955,
  -32766, -31988, -29754, -26252, -21712, -16387, -10537,  -4418,
    1726,   7680,  13252,  18287,  22661,  26287,  29107,  31097,
   32257,  32610,  32199,  31083,  29331,  27022,  24237,  21062,
   17579,  13872,  10018,   6089,   2152,  -1734,  -5515,  -9146,
  -12587, -15807, -18779, -21484, -23907, -26038, -27872, -29408,
  -30647, -31595, -32257, -32644, -32766, -32635, -32264, -31666,
  -30855, -29846, -28653, -27289, -25770, -24109, -22319, -20413,
  -18405, -16307, -14130, -11888,  -9591,  -7250,  -4877,  -2483,
     -78,   2327,   4722,   7095,   9435,  11732,  13975,  16151,
   18249,  20257,  22163,  23953,  25615,  27134,  28497,  29691,
   30700,  31510,  32108,  32479,  32610,  32488,  32101,  31439,
   30492,  29253,  27717,  25883,  23752,  21329,  18624,  15651,
   12431,   8990,   5359,   1578,  -2307,  -6245, -10174, -14028,
  -17735, -21217, -24393, -27177, -29487, -31238, -32354, -32765,
  -32412, -31253, -29263, -26442, -22817, -18442, -13407,  -7835,
   -1882,   4263,  10381,  16231,  21557,  26096,  29598,  31832,
   32610,  31800,  29341,  25263,  19695,  12871,   5130,  -3091,
  -11279, -18867, -25276, -29960, -32452, -32422, -29725, -24437,
  -16883,  -7636,   2505,  12574,  21519,  28311,  32069,  32191,
   28462,  21137,  10967,   -844, -12749, -23051, -30140, -32765,
  -30278, -22825, -11417,   2150,  15530,  26214,  32008,  31519,
   24543,  12248,  -2923, -17678, -28556, -32762, -28943, -17709,
   -1682,  15005,  27714,  32609,  27895,  14592,  -3491, -20710,
  -31300, -31354, -20413,  -1981,  17389,  30325,  31480,  19915,
   19915
};

// Braids reads these with Interpolate88(table, sample + 32768): the int16
// sample becomes a uint16 index, its top 8 bits select the entry and its low 8
// interpolate toward the next. `x` here is that same input, normalized.
inline float ReadShaper(const int16_t* table, float x) {
  float index = (x + 1.0f) * 128.0f;
  CONSTRAIN(index, 0.0f, 255.999f);
  const int integral = static_cast<int>(index);
  const float fractional = index - static_cast<float>(integral);
  const float a = static_cast<float>(table[integral]);
  const float b = static_cast<float>(table[integral + 1]);
  return (a + (b - a) * fractional) / 32768.0f;
}

// Braids' triangle: a phase ramp folded about its midpoint, full scale.
// Verified against the integer original -- (phase_16 << 1) ^ (sign ? 0xffff :
// 0) then += 32768, read as int16 -- at every eighth of a cycle.
inline float Triangle(float phase) {
  const float t = phase < 0.5f ? phase : 1.0f - phase;
  return 4.0f * t - 1.0f;
}

// Braids' wav_sine is NOT Plaits' lut_sine. It starts at -32512 and reaches
// zero a quarter of the way through, so it is -cos(2*pi*phase); Plaits'
// starts at 0 and is sin(2*pi*phase). A quarter cycle apart.
//
// For a linear oscillator that is inaudible. Into a wavefolder it is not: the
// folder's lobes land at different points of the cycle, and the sine folder is
// then summed against a triangle folder whose phase did not move, so the
// crossfade produces a different waveform at every blend setting. The first
// A/B of this engine measured 8.6 dB of spectral difference at shallow fold,
// which is what that looks like from the outside.
const float kBraidsSinePhaseOffset = 0.75f;

inline float BraidsSine(float phase) {
  phase += kBraidsSinePhaseOffset;
  if (phase >= 1.0f) {
    phase -= 1.0f;
  }
  return Sine(phase);
}

// The depth guard, expressed as a gain that falls from 1 to 0 over `span`
// semitones above `threshold`. Braids computes this from its integer pitch;
// the internal rate is the same 192 kHz on both sides, so the thresholds carry
// over unchanged (SPEC R5).
inline float DepthGuard(float note, float threshold, float span) {
  if (note <= threshold) {
    return 1.0f;
  }
  const float attenuation = 1.0f - (note - threshold) / span;
  return attenuation > 0.0f ? attenuation : 0.0f;
}

}  // namespace

void FoldEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void FoldEngine::Reset() {
  phase_ = 0.0f;
  frequency_ = 0.01f;
  depth_ = kFoldMinDepth;
  blend_ = 0.0f;
  symmetry_ = 0.0f;
  drive_ = 1.0f;
  downsampler_state_ = 0.0f;
  downsampler_state_aux_ = 0.0f;
  dc_input_ = 0.0f;
  dc_output_ = 0.0f;
  dc_input_aux_ = 0.0f;
  dc_output_aux_ = 0.0f;
}

void FoldEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    phase_ = 0.0f;
  }

  const bool stereo = PLAITS_STEREO_FOLD && parameters.stereo;

  const float frequency = NoteToFrequency(parameters.note);

  // Braids' two knobs, one each: TIMBRE is the fold depth for both folders,
  // COLOR is the crossfade between them.
  const float target_depth = kFoldMinDepth + kFoldDepthRange * parameters.timbre;
  const float target_blend = parameters.harmonics;

  const float target_symmetry = (parameters.morph - 0.5f) * 2.0f *
      kFoldMaxSymmetry;
  const float target_drive = ApplyMacro(1.0f, 0.5f, 2.5f, parameters.macro);

  // Braids attenuates each folder's depth as the note climbs, the triangle
  // first. Both guards are anti-aliasing measures against a 192 kHz internal
  // rate, which this port shares, so the thresholds transfer as written.
  const float tri_guard = DepthGuard(
      parameters.note, kFoldTriGuardNote, kFoldTriGuardSpan);
  const float sine_guard = DepthGuard(
      parameters.note, kFoldSineGuardNote, kFoldSineGuardSpan);

  ParameterInterpolator fm(&frequency_, frequency, size);
  ParameterInterpolator depth_modulation(&depth_, target_depth, size);
  ParameterInterpolator blend_modulation(&blend_, target_blend, size);
  ParameterInterpolator symmetry_modulation(&symmetry_, target_symmetry, size);
  ParameterInterpolator drive_modulation(&drive_, target_drive, size);

  Downsampler downsampler(&downsampler_state_);
  Downsampler downsampler_aux(&downsampler_state_aux_);

  while (size--) {
    const float f = fm.Next();
    const float depth = depth_modulation.Next();
    const float blend = blend_modulation.Next();
    const float symmetry = symmetry_modulation.Next();
    const float drive = drive_modulation.Next();

    const float sine_depth = depth * sine_guard * drive;
    const float tri_depth = depth * tri_guard * drive;

    float blend_main = blend;
    float blend_aux = 1.0f - blend;
    if (stereo) {
      blend_main = blend - kFoldStereoBlend;
      blend_aux = blend + kFoldStereoBlend;
      CONSTRAIN(blend_main, 0.0f, 1.0f);
      CONSTRAIN(blend_aux, 0.0f, 1.0f);
    }

    const float increment = f * 0.25f;
    for (int j = 0; j < 4; ++j) {
      phase_ += increment;
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      }

      // Braids drives the sine folder from its sine table and the triangle
      // folder from a raw ramp fold, both at full scale before the depth gain.
      const float sine_source = BraidsSine(phase_) * sine_depth + symmetry;
      const float tri_source = Triangle(phase_) * tri_depth + symmetry;

      const float folded_sine = ReadShaper(kSineFold, sine_source);
      const float folded_tri = ReadShaper(kTriFold, tri_source);

      downsampler.Accumulate(j, folded_sine +
          (folded_tri - folded_sine) * blend_main);
      downsampler_aux.Accumulate(j, folded_sine +
          (folded_tri - folded_sine) * blend_aux);
    }

    // Both shaper curves are asymmetric by construction (SPEC notes the
    // -32766..32682 span), and Symmetry deliberately adds an offset, so the
    // output carries DC that has to come out before the gain stage. R1 then
    // applies: an engine with a DC blocker cannot pin its post-blocker peak,
    // so this engine registers negative gains and takes the limiter path.
    const float raw = downsampler.Read();
    const float raw_aux = downsampler_aux.Read();

    ONE_POLE(dc_input_, raw, 0.001f);
    ONE_POLE(dc_input_aux_, raw_aux, 0.001f);
    dc_output_ = raw - dc_input_;
    dc_output_aux_ = raw_aux - dc_input_aux_;

    *out++ = dc_output_;
    *aux++ = dc_output_aux_;
  }
}

}  // namespace plaits
