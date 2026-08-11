// Copyright 1995-2023 Perry R. Cook and Gary P. Scavone.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// A lip-reed brass instrument. See the header for what the research found.

#include "plaits/dsp/engine2/brass_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "plaits/build_config.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void BrassEngine::Init(BufferAllocator* allocator) {
  delay_line_ = allocator->Allocate<float>(kBrassDelaySize);
  Reset();
}

void BrassEngine::Reset() {
  for (int i = 0; i < kBrassDelaySize; ++i) {
    delay_line_[i] = 0.0f;
  }
  delay_write_ = 0;
  lip_x_ = 0.0f;
  lip_v_ = 0.0f;
  bell_lp_ = 0.0f;
  prev_arriving_ = 0.0f;
  junction_ = 0.0f;
  dc_x1_ = 0.0f;
  dc_y1_ = 0.0f;
  out_dc_x1_ = 0.0f;
  out_dc_y1_ = 0.0f;
  aux_dc_x1_ = 0.0f;
  aux_dc_y1_ = 0.0f;
}

void BrassEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Reset();
  }

  float note = parameters.note;
  while (note < kBrassLowestNote) {
    note += 12.0f;
  }
  float frequency = NoteToFrequency(note) * kSampleRate;
  CONSTRAIN(frequency, 20.0f, 2000.0f);
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  const float frequency_offset_scale = frequency /
      (NoteToFrequency(parameters.note) * kSampleRate);
#endif

  // The bore is tuned an octave below the played note, so the played note is
  // its second partial.
  const float bore_frequency = frequency * 0.5f;

  // Which partials this bore can offer. Computed from the uncompensated bore,
  // because the compensation below needs the partial number first.
  int top = kBrassLastPartial;
  while (top > kBrassFirstPartial &&
      bore_frequency * float(top) > kBrassHighestPartial) {
    --top;
  }

  // HARMONICS is a continuous PARTIAL INDEX, not a lip frequency -- a linear
  // lip sweep would spend about 40% of the knob in the silent gaps between
  // lock zones.
  const float span = float(top - kBrassFirstPartial);
  float position = float(kBrassFirstPartial) + parameters.harmonics * span;
  int partial = static_cast<int>(position);
  CONSTRAIN(partial, kBrassFirstPartial, top);
  const float within = position - static_cast<float>(partial);

  // Null the lip's pitch pull for the partial actually sounding.
  const float pull_cents = kBrassPullOffset +
      kBrassPullScale / static_cast<float>(partial);
  // SemitonesToRatio, not powf: the firmware is bare-metal and libm's powf
  // pulls in __errno, which does not link (helix_engine.cc has the full note).
  // Cents/100 is semitones, and the LUT is exact enough for a pull this small.
  const float pull_ratio = SemitonesToRatio(pull_cents * 0.01f);
  float length = kSampleRate / bore_frequency * pull_ratio;
  CONSTRAIN(length, 8.0f, float(kBrassDelaySize - 2));

  const float actual_bore = kSampleRate / length;
  const float lip_frequency = actual_bore * static_cast<float>(partial) *
      (kBrassZoneBase + kBrassZoneSpan * within);
  float lip_clamped = lip_frequency;
  CONSTRAIN(lip_clamped, 10.0f, 0.40f * kSampleRate);
  const float lip_w = 2.0f * float(M_PI) * lip_clamped / kSampleRate;

  // TIMBRE is breath pressure. Below the oscillation threshold the model is
  // correctly silent, so the range starts above it.
  const float mouth = 0.18f + 0.50f * parameters.timbre;

  // MORPH is the bell. Its radiation corner tracks the played note rather than
  // sitting at a fixed frequency: a real bell's fixed cutoff is exactly why its
  // low register radiates so little, and that measured as a 30 dB level slide
  // across the keyboard.
  const float cutoff_ratio = 4.0f + 22.0f * parameters.morph;
  float damp = 2.0f * float(M_PI) * cutoff_ratio * frequency / kSampleRate;
  CONSTRAIN(damp, 0.02f, 0.90f);

  const float read_offset = length;

  // MACRO is the buzz: how far the lips swing into closure each cycle. It is
  // the valve's rest opening, and it is the difference between a soft flutter
  // and a real mouthpiece buzz.
  //
  // The direction is the opposite of the obvious one, and it was measured
  // rather than reasoned. Closing the valve DOWN does not buzz -- a small
  // opening passes a small flow, so the lip barely swings and closure FALLS
  // (1.4% of the cycle at 0.10, against 1.9% at the 0.60 default). Buzz comes
  // from DRIVE: open the valve further, pass more flow, and the lip swings hard
  // enough to slam shut. Measured across this range, closure goes 1.5% -> 5.8%
  // and the energy above the 4th harmonic rises about 20 dB.
  //
  // 0.5 is the value the engine shipped with, deliberately: MACRO is Plaits'
  // FOURTH control and rests at its midpoint whenever the menu is not set to
  // expose it, so the default instrument has to be -- and is bit-identical to
  // -- the one at 0.5.
  //
  // The top stops at 0.90 with real margin, because finding 3's cliff is just
  // above it: at a rest opening of 1.00 the level is already 11 dB down, and by
  // 1.10 the model has stopped selecting partials altogether (flat staircase,
  // 65 Hz, near-silent). Do not widen this without re-running a HARMONICS
  // staircase -- and compare it against the SHIPPED value in the same harness,
  // because an 8-segment sweep shows dropouts at every setting including the
  // default, which reads as a defect and is not one.
  // Expressed as a scale on kBrassRestOpening rather than as bare numbers, so
  // the default and the knob cannot drift apart: 0.5 lands exactly on it.
  const float rest_opening =
      kBrassRestOpening * (0.5f + parameters.macro);

  // The steepening displacement is bounded by the bore length, not by a fixed
  // number of samples: a short bore has less room before the read pointer would
  // overtake the write pointer.
  const float steepening_limit = kBrassSteepeningLimit * read_offset;

  for (size_t i = 0; i < size; ++i) {
    float sample_read_offset = read_offset;
    float sample_lip_w = lip_w;
    float sample_damp = damp;
    float sample_steepening_limit = steepening_limit;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      float sample_frequency = frequency +
          parameters.frequency_offset[i] * kSampleRate *
          frequency_offset_scale;
      CONSTRAIN(sample_frequency, 20.0f, 2000.0f);
      const float sample_bore_frequency = sample_frequency * 0.5f;
      sample_read_offset =
          kSampleRate / sample_bore_frequency * pull_ratio;
      CONSTRAIN(
          sample_read_offset, 8.0f, float(kBrassDelaySize - 2));
      const float sample_actual_bore = kSampleRate / sample_read_offset;
      float sample_lip_frequency =
          sample_actual_bore * static_cast<float>(partial) *
          (kBrassZoneBase + kBrassZoneSpan * within);
      CONSTRAIN(sample_lip_frequency, 10.0f, 0.40f * kSampleRate);
      sample_lip_w =
          2.0f * float(M_PI) * sample_lip_frequency / kSampleRate;
      sample_damp =
          2.0f * float(M_PI) * cutoff_ratio * sample_frequency / kSampleRate;
      CONSTRAIN(sample_damp, 0.02f, 0.90f);
      sample_steepening_limit =
          kBrassSteepeningLimit * sample_read_offset;
    }
#endif
    // Nonlinear propagation: a high-pressure wave arrives early. Driven by the
    // previous arriving sample rather than the current one, which would be
    // circular -- one sample of lag against a bore hundreds of samples long.
    float steepening = kBrassSteepening * prev_arriving_;
    if (steepening > sample_steepening_limit) {
      steepening = sample_steepening_limit;
    } else if (steepening < -sample_steepening_limit) {
      steepening = -sample_steepening_limit;
    }

    float read_position =
        static_cast<float>(delay_write_) -
        (sample_read_offset - steepening);
    while (read_position < 0.0f) {
      read_position += static_cast<float>(kBrassDelaySize);
    }
    const int integral = static_cast<int>(read_position);
    const float fractional = read_position - static_cast<float>(integral);
    const int a = integral % kBrassDelaySize;
    const int b = (integral + 1) % kBrassDelaySize;
    const float arriving = delay_line_[a] +
        (delay_line_[b] - delay_line_[a]) * fractional;
    prev_arriving_ = arriving;

    // The bell: what it does not reflect, it radiates.
    bell_lp_ += sample_damp * (arriving - bell_lp_);
    const float p_minus = kBrassReflection * bell_lp_;

    const float breath = mouth *
        (1.0f + kBrassNoiseGain * (2.0f * Random::GetFloat() - 1.0f));

    // The valve sees the TOTAL junction pressure, one sample late. Feeding it
    // the reflected wave instead is what stopped earlier attempts locking to
    // the bore at all.
    const float delta = breath - junction_;

    // Outward-striking lip: rising pressure opens it.
    const float target = delta / kBrassLipStiffness;
    lip_v_ += sample_lip_w * sample_lip_w * (target - lip_x_)
        - 2.0f * kBrassLipZeta * sample_lip_w * lip_v_;
    lip_x_ += lip_v_;

    float opening = rest_opening + lip_x_;
    if (opening < 0.0f) {
      // Lips closed: no flow, and they stop rather than keep travelling.
      opening = 0.0f;
      if (lip_v_ < 0.0f) {
        lip_v_ = 0.0f;
      }
    } else if (opening > kBrassMaxOpening) {
      opening = kBrassMaxOpening;
    }

    // Bernoulli flow through the opening.
    // stmlib::Sqrt is one VSQRT instruction on the M4F; sqrtf is a libm call
    // that both fails to link here (__errno) and costs far more per sample.
    const float flow = opening *
        (delta >= 0.0f ? Sqrt(delta) : -Sqrt(-delta));

    const float p_plus = p_minus + kBrassBoreImpedance * flow;
    junction_ = p_plus + p_minus;

    // The loop is non-inverting so it has a DC mode; without this the model
    // parks on it and the bore length stops mattering.
    const float blocked = p_plus - dc_x1_ + 0.9995f * dc_y1_;
    dc_x1_ = p_plus;
    dc_y1_ = blocked;

    delay_line_[delay_write_] = blocked;
    delay_write_ = (delay_write_ + 1) % kBrassDelaySize;

    // Blowing produces a steady flow as well as an oscillation, so both taps
    // carry a large DC term and both have to be blocked. Unblocked, OUT
    // measured RMS 27923 against a DC component of 26548.
    const float out_ac = junction_ - out_dc_x1_ + 0.9995f * out_dc_y1_;
    out_dc_x1_ = junction_;
    out_dc_y1_ = out_ac;

    const float valve_flow = kBrassBoreImpedance * flow;
    const float aux_ac = valve_flow - aux_dc_x1_ + 0.9995f * aux_dc_y1_;
    aux_dc_x1_ = valve_flow;
    aux_dc_y1_ = aux_ac;

    // OUT is the pressure inside the mouthpiece: flat to 0.7 dB from note 40
    // up, where the radiated tap humps by 17 dB.
    out[i] = SoftClip(out_ac * 1.6f);
    // AUX is the valve flow -- the same note heard as the buzz that drives it.
    aux[i] = SoftClip(aux_ac * 6.0f);
  }
}

}  // namespace plaits
