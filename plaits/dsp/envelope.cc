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

// round(65535 * t^1.7), t = i / 64. A gated VCA benefits from a gentle swell,
// but Elements' t^3.32 exciter curve spends too much of a long attack near
// silence before arriving abruptly. This keeps the onset soft while making the
// whole gesture audible.
const uint16_t kGatedAttackCurve[kCurveTableSize + 1] = {
  0, 56, 181, 361, 588, 859, 1172, 1523, 1911, 2334, 2792, 3283, 3807,
  4362, 4947, 5563, 6208, 6882, 7585, 8315, 9072, 9857, 10668, 11505,
  12369, 13258, 14172, 15111, 16074, 17063, 18075, 19111, 20171, 21254,
  22360, 23490, 24642, 25817, 27015, 28234, 29476, 30740, 32025, 33332,
  34661, 36010, 37381, 38773, 40186, 41620, 43074, 44549, 46044, 47559,
  49095, 50651, 52226, 53821, 55436, 57071, 58725, 60399, 62092, 63804,
  65535,
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

float OneKnobEnvelope::GatedAttackCurve(float phase) {
  return LookupCurve(kGatedAttackCurve, phase);
}

float OneKnobEnvelope::InverseGatedAttackCurve(float value) {
  if (value <= 0.0f) {
    return 0.0f;
  }
  if (value >= 1.0f) {
    return 1.0f;
  }
  const float scaled = value * 65535.0f;
  int lower = 0;
  int upper = kCurveTableSize;
  while (upper - lower > 1) {
    const int midpoint = (lower + upper) >> 1;
    if (static_cast<float>(kGatedAttackCurve[midpoint]) <= scaled) {
      lower = midpoint;
    } else {
      upper = midpoint;
    }
  }
  const float a = static_cast<float>(kGatedAttackCurve[lower]);
  const float b = static_cast<float>(kGatedAttackCurve[upper]);
  const float fractional = (scaled - a) / (b - a);
  return (static_cast<float>(lower) + fractional) /
      static_cast<float>(kCurveTableSize);
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

float OneKnobEnvelope::Process(
    float shape,
    bool gate,
    bool rising_edge,
    Profile profile,
    Mode mode) {
  if (shape < 0.0f) {
    shape = 0.0f;
  } else if (shape > 1.0f) {
    shape = 1.0f;
  }

  float attack;
  float decay_release;
  float sustain;
  bool gated;

  if (mode == MODE_TRIGGERED) {
    // Treat the knob as a path through attack/decay space rather than forcing
    // one time to be the inverse of the other. The uniformly spaced waypoints
    // move through soft plucks, ringing envelopes, slow-attack/slow-decay
    // gestures, and finally a slow-attack/short-decay reverse pluck. Time itself
    // is already mapped approximately logarithmically by TimeIncrement(). The
    // 19 ms / 80 ms floor avoids spending travel on sub-20-ms attacks and
    // heavily suppressed decays that tend to collapse into clicks or blips.
    static const float kAttack[] = {
      0.20f, 0.215f, 0.24f, 0.28f, 0.34f,
      0.44f, 0.56f, 0.68f, 0.80f,
    };
    static const float kDecay[] = {
      0.32f, 0.40f, 0.52f, 0.66f, 0.82f,
      0.84f, 0.80f, 0.68f, 0.42f,
    };
    float waypoint = shape * 8.0f;
    int segment = static_cast<int>(waypoint);
    float amount = waypoint - static_cast<float>(segment);
    if (segment >= 8) {
      segment = 7;
      amount = 1.0f;
    }
    attack = kAttack[segment] +
        (kAttack[segment + 1] - kAttack[segment]) * amount;
    decay_release = kDecay[segment] +
        (kDecay[segment + 1] - kDecay[segment]) * amount;
    if (profile == PROFILE_ELEMENTS_RESONATOR) {
      // Resonators supply some of their own temporal body. Preserve the same
      // shape progression while gently compressing only the long end.
      attack = 0.20f + (attack - 0.20f) * 0.85f;
      decay_release = 0.32f + (decay_release - 0.32f) * 0.85f;
    }
    sustain = 0.0f;
    gated = false;
  } else {
    // Preserve the intuitive clockwise-is-slower gesture, but place explicit
    // waypoints in perceived time rather than linearly sweeping the already
    // nonlinear Elements time control. The synth path spans 8 ms..2.6 s of
    // attack and 30 ms..4.5 s of release. Medium gestures arrive early enough
    // to make the first third of the knob distinct. Resonators share the useful
    // fast end but compress the acoustic tail to 1.2 s / 3.0 s.
    static const float kSynthAttack[] = {
      0.140606f, 0.221317f, 0.322106f, 0.405549f, 0.500136f,
      0.584476f, 0.653884f, 0.727494f, 0.781246f,
    };
    static const float kSynthRelease[] = {
      0.235792f, 0.333503f, 0.456250f, 0.541704f, 0.626280f,
      0.709131f, 0.774362f, 0.834983f, 0.882649f,
    };
    static const float kResonatorAttack[] = {
      0.140606f, 0.204219f, 0.287373f, 0.362348f, 0.442869f,
      0.515241f, 0.574760f, 0.626280f, 0.653884f,
    };
    static const float kResonatorRelease[] = {
      0.235792f, 0.315960f, 0.417285f, 0.500136f, 0.574760f,
      0.653884f, 0.718523f, 0.767248f, 0.806767f,
    };
    const float* attack_waypoints = profile == PROFILE_SYNTH
        ? kSynthAttack
        : kResonatorAttack;
    const float* release_waypoints = profile == PROFILE_SYNTH
        ? kSynthRelease
        : kResonatorRelease;
    float waypoint = shape * 8.0f;
    int segment = static_cast<int>(waypoint);
    float amount = waypoint - static_cast<float>(segment);
    if (segment >= 8) {
      segment = 7;
      amount = 1.0f;
    }
    attack = attack_waypoints[segment] +
        (attack_waypoints[segment + 1] - attack_waypoints[segment]) * amount;
    decay_release = release_waypoints[segment] +
        (release_waypoints[segment + 1] - release_waypoints[segment]) * amount;
    sustain = 1.0f;
    gated = true;
  }

  if (rising_edge) {
    if (mode == MODE_TRIGGERED) {
      // Triggered mode uses a linear attack, so its current amplitude is also
      // the phase needed to resume that attack without a discontinuity. An
      // edge during attack therefore does not restart the clock, while an edge
      // during decay turns smoothly back toward the peak. This remains fully
      // retriggerable without the slow-attack staircase/latch caused by
      // resetting phase to zero from the current value.
      start_value_ = 0.0f;
      phase_ = value_;
    } else {
      // Reverse from a release without restarting a complete attack from the
      // current level. Reconstructing the absolute attack phase keeps the edge
      // continuous and makes only the perceptually remaining rise play out.
      start_value_ = 0.0f;
      phase_ = InverseGatedAttackCurve(value_);
    }
    segment_ = SEGMENT_ATTACK;
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
  // Elements' quartic attack deliberately stays near zero before arriving
  // abruptly. That articulation works as an exciter contour, but turns a VCA
  // contour into a late blip. Triggered mode is linear; gated mode uses a
  // gentler t^1.7 swell.
  float curve = ExponentialCurve(phase_);
  if (attack_segment) {
    curve = mode == MODE_TRIGGERED
        ? phase_
        : GatedAttackCurve(phase_);
  }
  value_ = start_value_ + (target - start_value_) * curve;
  phase_ += TimeIncrement(attack_segment ? attack : decay_release);
  return value_;
}

#if defined(TEST)
float OneKnobEnvelope::TestGatedAttackCurve(float phase) {
  return GatedAttackCurve(phase);
}

float OneKnobEnvelope::TestExponentialCurve(float phase) {
  return ExponentialCurve(phase);
}

float OneKnobEnvelope::TestTimeIncrement(float time) {
  return TimeIncrement(time);
}
#endif

}  // namespace plaits
