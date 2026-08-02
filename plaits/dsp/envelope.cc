// Copyright 2014 Emilie Gillet.
// Copyright 2026 Rubato Audio.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// -----------------------------------------------------------------------------
//
// Elements-style one-knob envelope for Plaits.

#include "plaits/dsp/envelope.h"

#include "stmlib/dsp/units.h"
#include "plaits/dsp/dsp.h"

namespace plaits {

namespace {

const int kCurveTableSize = 64;
const int kTimeTableSize = 128;

// round(65535 * t^3.32), t = i / 64. This is Elements' quartic attack
// (the historical name is retained even though the fitted exponent is 3.32).
const uint16_t kQuarticCurve[kCurveTableSize + 1] = {
  0, 0, 1, 3, 7, 14, 25, 42, 66, 97, 138, 189, 253,
  330, 422, 530, 657, 804, 972, 1163, 1378, 1621, 1891, 2192, 2525, 2891,
  3294, 3733, 4212, 4733, 5297, 5906, 6562, 7268, 8025, 8836, 9702, 10626,
  11610, 12656, 13766, 14942, 16186, 17501, 18889, 20353, 21893, 23514,
  25216, 27003, 28876, 30838, 32892, 35039, 37282, 39624, 42067, 44613,
  47265, 50025, 52895, 55879, 58979, 62197, 65535,
};

// round(65535 * (1 - exp(-4t)) / (1 - exp(-4))), t = i / 64.
const uint16_t kExponentialCurve[kCurveTableSize + 1] = {
  0, 4045, 7844, 11414, 14767, 17917, 20876, 23656, 26267, 28720, 31025,
  33190, 35224, 37134, 38929, 40615, 42199, 43687, 45085, 46398, 47631,
  48790, 49879, 50901, 51862, 52765, 53612, 54409, 55157, 55860, 56520,
  57140, 57723, 58270, 58785, 59268, 59721, 60148, 60548, 60924, 61278,
  61610, 61922, 62215, 62490, 62749, 62991, 63220, 63434, 63635, 63825,
  64002, 64169, 64326, 64473, 64612, 64742, 64864, 64979, 65086, 65188,
  65283, 65372, 65456, 65535,
};

// Elements' time law, stored as log2(phase increment) in Q10 at 129 evenly
// spaced control positions. The source range is 0.5 ms .. 8 s, gamma 0.175,
// evaluated for Plaits' nominal 4 kHz block rate. Interpolating in log space
// keeps the worst rate error below 0.12% including quantization, while this
// table occupies 258 bytes rather than 516 bytes of floats.
const int16_t kTimeIncrementLog2[kTimeTableSize + 1] = {
  -1024, -1312, -1590, -1860, -2121, -2375, -2621, -2860, -3092, -3318,
  -3539, -3753, -3963, -4167, -4367, -4561, -4752, -4938, -5120, -5299,
  -5474, -5645, -5813, -5977, -6138, -6297, -6452, -6605, -6755, -6902,
  -7047, -7189, -7329, -7467, -7602, -7735, -7867, -7996, -8123, -8249,
  -8372, -8494, -8614, -8732, -8849, -8964, -9078, -9190, -9301, -9410,
  -9518, -9624, -9729, -9833, -9935, -10037, -10137, -10236, -10334,
  -10430, -10526, -10620, -10714, -10806, -10898, -10988, -11078, -11166,
  -11254, -11341, -11427, -11512, -11596, -11679, -11761, -11843, -11924,
  -12004, -12084, -12162, -12240, -12317, -12394, -12470, -12545, -12620,
  -12693, -12767, -12839, -12911, -12982, -13053, -13123, -13193, -13262,
  -13330, -13398, -13466, -13532, -13599, -13665, -13730, -13795, -13859,
  -13923, -13986, -14049, -14111, -14173, -14235, -14296, -14356, -14416,
  -14476, -14536, -14594, -14653, -14711, -14769, -14826, -14883, -14939,
  -14996, -15051, -15107, -15162, -15217, -15271, -15325,
};

float LookupCurve(const uint16_t* table, float phase) {
  if (phase <= 0.0f) {
    return 0.0f;
  }
  if (phase >= 1.0f) {
    return 1.0f;
  }
  const float index = phase * static_cast<float>(kCurveTableSize);
  const int integral = static_cast<int>(index);
  const float fractional = index - static_cast<float>(integral);
  const float a = static_cast<float>(table[integral]);
  const float b = static_cast<float>(table[integral + 1]);
  return (a + (b - a) * fractional) * (1.0f / 65535.0f);
}

}  // namespace

void OneKnobEnvelope::Init() {
  segment_ = SEGMENT_DONE;
  phase_ = 0.0f;
  start_value_ = 0.0f;
  value_ = 0.0f;
}

float OneKnobEnvelope::QuarticCurve(float phase) {
  return LookupCurve(kQuarticCurve, phase);
}

float OneKnobEnvelope::ExponentialCurve(float phase) {
  return LookupCurve(kExponentialCurve, phase);
}

float OneKnobEnvelope::TimeIncrement(float time) {
  if (time <= 0.0f) {
    time = 0.0f;
  } else if (time >= 1.0f) {
    time = 1.0f;
  }

  const float index = time * static_cast<float>(kTimeTableSize);
  int integral = static_cast<int>(index);
  float fractional = index - static_cast<float>(integral);
  if (integral >= kTimeTableSize) {
    integral = kTimeTableSize - 1;
    fractional = 1.0f;
  }
  const float a = static_cast<float>(kTimeIncrementLog2[integral]);
  const float b = static_cast<float>(kTimeIncrementLog2[integral + 1]);
  const float log2_increment = (a + (b - a) * fractional) * (1.0f / 1024.0f);

  // The table is normalized for the 48 kHz / 12-sample production block rate.
  // Keep the duration correct in host experiments that change either constant.
  const float control_rate_correction =
      4000.0f * static_cast<float>(kBlockSize) / kSampleRate;
  return stmlib::Exp2Safe(log2_increment) * control_rate_correction;
}

float OneKnobEnvelope::Process(float shape, bool gate, bool rising_edge) {
  if (shape < 0.0f) {
    shape = 0.0f;
  } else if (shape > 1.0f) {
    shape = 1.0f;
  }

  float attack;
  float decay_release;
  float sustain;
  bool gated;
  if (shape < 0.4f) {
    attack = 0.15f + 0.75f * shape;
    decay_release = attack * 1.8f;
    sustain = 0.0f;
    gated = false;
  } else if (shape < 0.6f) {
    attack = 0.45f;
    decay_release = 0.81f;
    sustain = (shape - 0.4f) * 5.0f;
    gated = true;
  } else {
    attack = 0.15f + 0.75f * (1.0f - shape);
    decay_release = attack * 1.8f;
    sustain = 1.0f;
    gated = true;
  }

  if (rising_edge) {
    start_value_ = segment_ == SEGMENT_DONE ? 0.0f : value_;
    segment_ = SEGMENT_ATTACK;
    phase_ = 0.0f;
  } else if (segment_ == SEGMENT_SUSTAIN && !gated) {
    // If the knob crosses live from the gated half into the one-shot half,
    // continue smoothly into the newly selected AD region's release instead
    // of leaving the previous sustain latched.
    start_value_ = value_;
    segment_ = SEGMENT_RELEASE;
    phase_ = 0.0f;
  } else if (gated && !gate &&
             segment_ != SEGMENT_RELEASE && segment_ != SEGMENT_DONE) {
    // The sustain half is a real gated envelope. A low gate can interrupt the
    // attack or decay, just as it does in Elements.
    start_value_ = value_;
    segment_ = SEGMENT_RELEASE;
    phase_ = 0.0f;
  } else if (phase_ >= 1.0f) {
    phase_ = 0.0f;
    if (segment_ == SEGMENT_ATTACK) {
      start_value_ = 1.0f;
      segment_ = SEGMENT_DECAY;
    } else if (segment_ == SEGMENT_DECAY) {
      start_value_ = sustain;
      if (!gated) {
        value_ = 0.0f;
        segment_ = SEGMENT_DONE;
      } else if (gate) {
        value_ = sustain;
        segment_ = SEGMENT_SUSTAIN;
      } else {
        segment_ = SEGMENT_RELEASE;
      }
    } else if (segment_ == SEGMENT_RELEASE) {
      value_ = 0.0f;
      segment_ = SEGMENT_DONE;
    }
  }

  if (segment_ == SEGMENT_DONE) {
    value_ = 0.0f;
    return value_;
  }
  if (segment_ == SEGMENT_SUSTAIN) {
    // Sustain is part of the one-knob control. Follow live moves within the
    // gated region instead of freezing the value reached at the end of decay.
    value_ = sustain;
    start_value_ = sustain;
    return value_;
  }

  const bool attack_segment = segment_ == SEGMENT_ATTACK;
  const float target = attack_segment
      ? 1.0f
      : (segment_ == SEGMENT_DECAY ? sustain : 0.0f);
  const float curve = attack_segment
      ? QuarticCurve(phase_)
      : ExponentialCurve(phase_);
  value_ = start_value_ + (target - start_value_) * curve;
  phase_ += TimeIncrement(attack_segment ? attack : decay_release);
  return value_;
}

#if defined(TEST)
float OneKnobEnvelope::TestQuarticCurve(float phase) {
  return QuarticCurve(phase);
}

float OneKnobEnvelope::TestExponentialCurve(float phase) {
  return ExponentialCurve(phase);
}

float OneKnobEnvelope::TestTimeIncrement(float time) {
  return TimeIncrement(time);
}
#endif

}  // namespace plaits
