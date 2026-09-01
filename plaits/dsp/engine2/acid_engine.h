// Copyright 2026 Dylan Bolink.
// Copyright 2009 Robin Schmidt (www.rs-met.com) — the feedback-path highpass, the
// feedback waveshaper and the resonance taper come from Open303, MIT licensed.
// SPDX-License-Identifier: MIT
//
// Acid: a TB-303-shaped bassline voice. One oscillator into a four-pole ladder, then a
// diode-style clipper. It owns no envelope: the cutoff sweep arrives summed into
// parameters.morph, so DECAY is the filter decay and the MORPH attenuverter is ENV MOD.

#ifndef PLAITS_DSP_ENGINE2_ACID_ENGINE_H_
#define PLAITS_DSP_ENGINE2_ACID_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

#ifndef PLAITS_STEREO_ACID
#define PLAITS_STEREO_ACID 1
#endif

#include "plaits/dsp/oscillator/variable_shape_oscillator.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"

namespace plaits {

#ifndef PLAITS_ACID_LADDER_HEADROOM
#define PLAITS_ACID_LADDER_HEADROOM 3.0f
#endif
const float kAcidLadderHeadroom = PLAITS_ACID_LADDER_HEADROOM;
const float kAcidLadderHeadroomInverse = 1.0f / kAcidLadderHeadroom;

#ifndef PLAITS_ACID_FOURTH_POLE_RATIO
#define PLAITS_ACID_FOURTH_POLE_RATIO 1.8333f
#endif
const float kAcidFourthPoleRatio = PLAITS_ACID_FOURTH_POLE_RATIO;

#ifndef PLAITS_ACID_FEEDBACK_HIGHPASS
#define PLAITS_ACID_FEEDBACK_HIGHPASS 0.0006f   // 29 Hz at 47872 Hz
#endif
const float kAcidFeedbackHighpass = PLAITS_ACID_FEEDBACK_HIGHPASS;

#ifndef PLAITS_ACID_PER_STAGE_SATURATION
#define PLAITS_ACID_PER_STAGE_SATURATION 0
#endif

#ifndef PLAITS_ACID_INVERT_OSCILLATOR
#define PLAITS_ACID_INVERT_OSCILLATOR 1
#endif

// Three poles at G and a fourth at H, each y = G*x + (1-G)*s, composed with x = u - k*y:
//
//   y = (H*G^3*u + H*G^2*(1-G)*s1 + H*G*(1-G)*s2 + H*(1-G)*s3 + (1-H)*s4) / (1 + k*H*G^3)
//
// which is what Feedback() evaluates.
class LadderFilter {
 public:
  LadderFilter() { }
  ~LadderFilter() { }

  void Init() {
    feedback_highpass_.Init();
    feedback_highpass_.set_f<stmlib::FREQUENCY_DIRTY>(kAcidFeedbackHighpass);
    Reset();
    set_f(0.01f);
    set_resonance(0.0f);
  }

  void Reset() {
    s_[0] = 0.0f;
    s_[1] = 0.0f;
    s_[2] = 0.0f;
    s_[3] = 0.0f;
    feedback_highpass_.Reset();
  }

  inline void set_f(float f) {
    const float g = stmlib::OnePole::tan<stmlib::FREQUENCY_DIRTY>(f);
    g_ = g / (1.0f + g);

    float f_fast = f * kAcidFourthPoleRatio;
    if (f_fast > 0.24f) {
      f_fast = 0.24f;
    }
    const float h = stmlib::OnePole::tan<stmlib::FREQUENCY_DIRTY>(f_fast);
    h_ = h / (1.0f + h);
    const float complement = 1.0f - g_;
    const float g_cubed = g_ * g_ * g_;
    hg_cubed_ = h_ * g_cubed;
    c_[0] = h_ * g_ * g_ * complement;
    c_[1] = h_ * g_ * complement;
    c_[2] = h_ * complement;
    c_[3] = 1.0f - h_;
  }

  inline void set_resonance(float k) {
    k_ = k;
    inv_denominator_ = 1.0f / (1.0f + k * hg_cubed_);
  }

  inline float Process(float u) {
    const float x = u - Feedback(u);
#if PLAITS_ACID_PER_STAGE_SATURATION
    float y = Stage(&s_[0], x, g_);
    y = Stage(&s_[1], y, g_);
    y = Stage(&s_[2], y, g_);
    y = Stage(&s_[3], y, h_);
#else
    float y = LinearStage(&s_[0], x, g_);
    y = LinearStage(&s_[1], y, g_);
    y = LinearStage(&s_[2], y, g_);
    y = LinearStage(&s_[3], y, h_);
#endif
    return y;
  }

 private:
  // A linear solve is enough: it only sizes the feedback, and the shaper and highpass can
  // only reduce it.
  inline float Feedback(float u) {
    const float state = c_[0] * s_[0] + c_[1] * s_[1] + c_[2] * s_[2] + c_[3] * s_[3];
    const float predicted = (hg_cubed_ * u + state) * inv_denominator_;
    return feedback_highpass_.Process<stmlib::FILTER_MODE_HIGH_PASS>(
        k_ * FeedbackShape(predicted));
  }

  // Open303's shaper. Divide-free, unlike stmlib::SoftLimit.
  static inline float FeedbackShape(float x) {
    const float limit = 1.41421356f;
    if (x > limit) {
      x = limit;
    } else if (x < -limit) {
      x = -limit;
    }
    return x - (1.0f / 6.0f) * x * x * x;
  }

  inline float LinearStage(float* s, float x, float g) {
    const float v = (x - *s) * g;
    const float y = v + *s;
    *s = y + v;
    return y;
  }

  inline float Stage(float* s, float x, float g) {
    const float v = kAcidLadderHeadroom * g *
        stmlib::SoftLimit((x - *s) * kAcidLadderHeadroomInverse);
    const float y = v + *s;
    *s = y + v;
    return y;
  }

  float g_;
  float h_;
  float hg_cubed_;
  float c_[4];
  float k_;
  float inv_denominator_;
  float s_[4];
  stmlib::OnePole feedback_highpass_;

  DISALLOW_COPY_AND_ASSIGN(LadderFilter);
};


const float kAcidClipPositive = 0.70f;

const float kAcidClipKnee = 15.0f / 8.0f;
const float kAcidClipKneePositive = kAcidClipKnee * kAcidClipPositive;
const float kAcidClipKneeNegative = kAcidClipKnee;
const float kAcidClipKneePositiveInverse = 1.0f / kAcidClipKneePositive;
const float kAcidClipKneeNegativeInverse = 1.0f / kAcidClipKneeNegative;

#ifndef PLAITS_ACID_TONE_STACK
#define PLAITS_ACID_TONE_STACK 0
#endif

#ifndef PLAITS_ACID_TONE_CORNER
#define PLAITS_ACID_TONE_CORNER 0.042f
#endif
#ifndef PLAITS_ACID_TONE_EMPHASIS
#define PLAITS_ACID_TONE_EMPHASIS 3.0f
#endif
const float kAcidToneCorner = PLAITS_ACID_TONE_CORNER;
const float kAcidToneEmphasis = PLAITS_ACID_TONE_EMPHASIS;
const float kAcidToneEmphasisInverse = 1.0f / kAcidToneEmphasis;

class AcidShaper {
 public:
  AcidShaper() { }
  ~AcidShaper() { }

  void Init() {
#if PLAITS_ACID_TONE_STACK
    pre_.Init();
    post_.Init();
    pre_.set_f<stmlib::FREQUENCY_DIRTY>(kAcidToneCorner);
    post_.set_f<stmlib::FREQUENCY_DIRTY>(kAcidToneCorner);
#endif
    Reset();
  }

  void Reset() {
    previous_x_ = 0.0f;
    previous_integral_ = 0.0f;
#if PLAITS_ACID_TONE_STACK
    pre_.Reset();
    post_.Reset();
#endif
  }

  inline float Process(float x) {
#if PLAITS_ACID_TONE_STACK
    const float low = pre_.Process<stmlib::FILTER_MODE_LOW_PASS>(x);
    x = low + kAcidToneEmphasis * (x - low);
#endif
    return Deemphasize(Clip(x));
  }

 private:
  inline float Deemphasize(float y) {
#if PLAITS_ACID_TONE_STACK
    const float low = post_.Process<stmlib::FILTER_MODE_LOW_PASS>(y);
    return low + kAcidToneEmphasisInverse * (y - low);
#else
    return y;
#endif
  }

  // Antialiased by differencing the curve's antiderivative across the step. Only valid for
  // a FIXED curve: a moving parameter puts two antiderivatives in one numerator, and the
  // divide by a small dx turns that into a full-scale spike.
  inline float Clip(float x) {
    const float integral = CurveIntegral(x);
    const float dx = x - previous_x_;
    // Too small a step to divide by: the ratio tends to the curve at the midpoint.
    const float y = (dx > -1e-5f && dx < 1e-5f)
        ? Curve(0.5f * (x + previous_x_))
        : (integral - previous_integral_) / dx;
    previous_x_ = x;
    previous_integral_ = integral;
    return y;
  }

  // y = lim * (u - 2u^3/3 + u^5/5), u = x/lim. Odd in u, so only the threshold differs.
  static inline float Curve(float x) {
    if (x >= 0.0f) {
      if (x >= kAcidClipKneePositive) return kAcidClipPositive;
      const float u = x * kAcidClipKneePositiveInverse;
      const float u2 = u * u;
      return kAcidClipKneePositive * u *
          (1.0f - u2 * (2.0f / 3.0f) + u2 * u2 * 0.2f);
    }
    if (x <= -kAcidClipKneeNegative) return -1.0f;
    const float u = x * kAcidClipKneeNegativeInverse;
    const float u2 = u * u;
    return kAcidClipKneeNegative * u *
        (1.0f - u2 * (2.0f / 3.0f) + u2 * u2 * 0.2f);
  }

  // Antiderivative of Curve: lim^2 * (u^2/2 - u^4/6 + u^6/30) inside the knee, linear
  // beyond. Matching at u = 1 puts the constant at -lim^2/6 on both sides.
  static inline float CurveIntegral(float x) {
    if (x >= 0.0f) {
      if (x >= kAcidClipKneePositive) {
        return kAcidClipPositive * x -
            kAcidClipKneePositive * kAcidClipKneePositive * (1.0f / 6.0f);
      }
      const float u = x * kAcidClipKneePositiveInverse;
      const float u2 = u * u;
      return kAcidClipKneePositive * kAcidClipKneePositive *
          u2 * (0.5f - u2 * (1.0f / 6.0f) + u2 * u2 * (1.0f / 30.0f));
    }
    if (x <= -kAcidClipKneeNegative) {
      return -x - kAcidClipKneeNegative * kAcidClipKneeNegative * (1.0f / 6.0f);
    }
    const float u = x * kAcidClipKneeNegativeInverse;
    const float u2 = u * u;
    return kAcidClipKneeNegative * kAcidClipKneeNegative *
        u2 * (0.5f - u2 * (1.0f / 6.0f) + u2 * u2 * (1.0f / 30.0f));
  }

  float previous_x_;
  float previous_integral_;
#if PLAITS_ACID_TONE_STACK
  stmlib::OnePole pre_;
  stmlib::OnePole post_;
#endif

  DISALLOW_COPY_AND_ASSIGN(AcidShaper);
};

class AcidEngine : public Engine {
 public:
  AcidEngine() { }
  ~AcidEngine() { }
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void LoadUserData(const uint8_t* user_data) { }
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_ACID; }

 private:
  VariableShapeOscillator oscillator_;

  LadderFilter filter_;

  // One per channel: each carries its own previous-sample antialiasing state.
  AcidShaper shaper_;
  AcidShaper shaper_right_;

  LadderFilter filter_aux_;

  float accent_;
  float accent_sweep_;


  float prev_makeup_;
  float prev_drive_;
  float prev_main_scale_;

  // VariableShapeOscillator returns 2*naive - 1, mean 1 - 2*pw, so MACRO's 12% pulse
  // arrives with +0.76 of DC — enough to eat the ladder's headroom and swamp the clipper's
  // asymmetry. A real ladder is AC-coupled at its input too.
  stmlib::OnePole input_dc_blocker_;

  // The asymmetric clipper makes DC by design; it comes off before the output stage.
  stmlib::OnePole dc_blocker_[2];

  DISALLOW_COPY_AND_ASSIGN(AcidEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_ACID_ENGINE_H_
