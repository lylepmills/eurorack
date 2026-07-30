// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Envelope for the internal LPG.

#ifndef PLAITS_DSP_ENVELOPE_H_
#define PLAITS_DSP_ENVELOPE_H_

#include "stmlib/stmlib.h"

namespace plaits {

class LPGEnvelope {
 public:
  LPGEnvelope() { }
  ~LPGEnvelope() { }
  
  inline void Init() {
    vactrol_state_ = 0.0f;
    gain_ = 1.0f;
    frequency_ = 0.5f;
    hf_bleed_ = 0.0f;
    ramp_up_ = false;
  }
  
  inline void Trigger() {
    ramp_up_ = true;
  }
  
  inline void ProcessPing(
      float attack,
      float short_decay,
      float decay_tail,
      float hf) {
    if (ramp_up_) {
      vactrol_state_ += attack;
      if (vactrol_state_ >= 1.0f) {
        vactrol_state_ = 1.0f;
        ramp_up_ = false;
      }
    }
    ProcessLP(ramp_up_ ? vactrol_state_ : 0.0f, short_decay, decay_tail, hf);
  }
  
  inline void ProcessLP(
      float level,
      float short_decay,
      float decay_tail,
      float hf) {
    float vactrol_input = level;
    float vactrol_error = (vactrol_input - vactrol_state_);
    float vactrol_state_2 = vactrol_state_ * vactrol_state_;
    float vactrol_state_4 = vactrol_state_2 * vactrol_state_2;
    float tail = 1.0f - vactrol_state_;
    float tail_2 = tail * tail;
    float vactrol_coefficient = (vactrol_error > 0.0f)
        ? 0.6f
        : short_decay + (1.0f - vactrol_state_4) * decay_tail;
    vactrol_state_ += vactrol_coefficient * vactrol_error;
    
    gain_ = vactrol_state_;
    frequency_ = 0.003f + 0.3f * vactrol_state_4 + hf * 0.04f;
    hf_bleed_ = (tail_2 + (1.0f - tail_2) * hf) * hf * hf;
  }
  
  inline float gain() const { return gain_; }
  inline float frequency() const { return frequency_; }
  inline float hf_bleed() const { return hf_bleed_; }
  
 private:
  float vactrol_state_;
  float gain_;
  float frequency_;
  float hf_bleed_;
  bool ramp_up_;
  
  DISALLOW_COPY_AND_ASSIGN(LPGEnvelope);
};

class DecayEnvelope {
 public:
  DecayEnvelope() { }
  ~DecayEnvelope() { }
  
  inline void Init() {
    value_ = 0.0f;
  }
  
  inline void Trigger() {
    value_ = 1.0f;
  }
  
  inline void Process(float decay) {
    value_ *= (1.0f - decay);
  }
  
  inline float value() const { return value_; }
  
 private:
  float value_;
  
  DISALLOW_COPY_AND_ASSIGN(DecayEnvelope);
};

// Gate-aware, one-knob envelope adapted from Elements' exciter envelope.
//
// The control travels through three regions:
//   0.0 .. 0.4  one-shot AD, from a short pluck to a slow swell
//   0.4 .. 0.6  the same slow attack/decay, with sustain rising from 0 to 1
//   0.6 .. 1.0  full sustain, with attack/release accelerating toward the end
//
// Elements treats the whole range as an ADSR. Plaits is normally driven by
// short trigger pulses rather than keyboard gates, so the zero-sustain region
// deliberately ignores the falling edge and completes its AD cycle. The
// sustain region follows the gate and releases as soon as it goes low.
//
// Process is called once per Plaits audio block. Curve and rate lookup tables
// live in envelope.cc; their compact fixed-point representation keeps the
// complete feature below 1 KB of table data without evaluating powf/expf in the
// audio callback.
class OneKnobEnvelope {
 public:
  OneKnobEnvelope() { }
  ~OneKnobEnvelope() { }

  void Init();

  float Process(float shape, bool gate, bool rising_edge);

  inline float value() const { return value_; }

#if defined(TEST)
  // Host-test access to the two approximations. These are not part of the
  // firmware API and compile out of production builds.
  static float TestQuarticCurve(float phase);
  static float TestExponentialCurve(float phase);
  static float TestTimeIncrement(float time);
#endif

 private:
  enum Segment {
    SEGMENT_ATTACK,
    SEGMENT_DECAY,
    SEGMENT_SUSTAIN,
    SEGMENT_RELEASE,
    SEGMENT_DONE
  };

  static float QuarticCurve(float phase);
  static float ExponentialCurve(float phase);
  static float TimeIncrement(float time);

  Segment segment_;
  float phase_;
  float start_value_;
  float value_;

  DISALLOW_COPY_AND_ASSIGN(OneKnobEnvelope);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENVELOPE_H_
