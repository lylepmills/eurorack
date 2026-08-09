// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' RING: a carrier ring-modulated by two independently detuned sines,
// through a saturating shaper.

#include "plaits/dsp/engine2/ring_mod_engine.h"
#include "plaits/build_config.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' wav_sine is -cos, a quarter period behind Plaits' lut_sine (see
// z_filter_engine.cc for the full note). Rather than pay for the +0.75 and a
// second wrap on every one of the six table reads per output sample, the three
// phase accumulators are STORED already carrying the offset, so the read is
// the cheap non-wrapping one. Safe because each phase is explicitly wrapped
// into [0, 1) before use and lut_sine carries 641 entries against the 513 a
// phase below 1.0 can reach.

// Braids' ws_moderate_overdrive is tanh(2x)/tanh(2) to within a thousandth,
// and SoftLimit is the Pade form of tanh -- so the table reduces to two
// multiplies and a divide. SoftLimit is asymptotic, not bounded, which R2
// forbids as an OUTPUT bound; here it is the in-loop saturation the model is
// built on, and the result is bounded by construction because the input is
// clamped to +-1 before it arrives.
inline float Overdrive(float x) {
  CONSTRAIN(x, -1.0f, 1.0f);
  return kRingModShaperNorm * SoftLimit(2.0f * x);
}

// Braids' knob -> detune quantization, in semitones. The >> 2 is an arithmetic
// shift on a signed value; the explicit floor avoids relying on that being
// implementation-defined while matching what both toolchains actually emit.
inline float BraidsDetune(float knob) {
  const int parameter = static_cast<int>(knob * 32767.0f);
  int units = parameter - 16384;
  units = units >= 0 ? (units >> 2) : -((-units + 3) >> 2);
  return static_cast<float>(units) * (1.0f / 128.0f);
}

}  // namespace

// Symmetric 15-tap halfband read out of a 16-entry ring buffer. `history`
// points at the buffer and the caller has already advanced the position, so
// index 0 is the newest sub-sample and the centre tap sits seven back -- on
// an even sub-sample, which is what keeps the decimation phase correct.
inline float RingModEngine::Decimate(const float* history) const {
  const int p = history_position_;
  return kRingModHalfbandCentre * history[(p - 7) & 15]
      + kRingModHalfband1 * (history[(p - 6) & 15] + history[(p - 8) & 15])
      + kRingModHalfband3 * (history[(p - 4) & 15] + history[(p - 10) & 15])
      + kRingModHalfband5 * (history[(p - 2) & 15] + history[(p - 12) & 15])
      + kRingModHalfband7 * (history[p] + history[(p - 14) & 15]);
}

void RingModEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void RingModEngine::Reset() {
  // phase_ tracks Braids' LOCAL carrier phase, the one it forms as
  // `phase_ + (1 << 30)` on entry and unwinds on exit. From a zeroed stored
  // phase that local phase starts a quarter cycle AHEAD of the modulators --
  // which is invisible until the detunes meet at zero and all three
  // oscillators would otherwise collapse into one.
  phase_ = 0.0f;          // Braids' local carrier phase 0.25, plus the 0.75 offset
  modulator_phase_ = 0.75f;
  modulator_phase_2_ = 0.75f;
  for (int i = 0; i < 16; ++i) {
    history_[i] = 0.0f;
    history_aux_[i] = 0.0f;
  }
  history_position_ = 0;
}

void RingModEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // Braids' sync zeroes the LOCAL carrier phase, so unlike the reset case
    // the quarter-cycle lead is dropped and all three start together.
    phase_ = 0.75f;
    modulator_phase_ = 0.75f;
    modulator_phase_2_ = 0.75f;
  }

  // Braids computes all three increments once per block; kBlockSize gives the
  // same 4 kHz update rate on both sides, so that is kept.
  float carrier_note = parameters.note;
  if (carrier_note > kRingModMaxPitch) {
    carrier_note = kRingModMaxPitch;
  }
  // Braids' detune is `(parameter_ - 16384) >> 2` in 1/128-semitone units, an
  // arithmetic shift and therefore a FLOOR (R8). Reproducing the quantization
  // matters at the knob centre: the shift lands on -1 rather than 0, so the
  // modulators sit a 128th of a semitone flat and drift slowly against the
  // carrier. A smooth float detune phase-locks all three there instead, which
  // is a static waveform where the hardware has a very slow beat.
  float note_1 = parameters.note + BraidsDetune(parameters.harmonics);
  float note_2 = parameters.note + BraidsDetune(parameters.timbre);
  if (note_1 > kRingModMaxPitch) {
    note_1 = kRingModMaxPitch;
  }
  if (note_2 > kRingModMaxPitch) {
    note_2 = kRingModMaxPitch;
  }

  const float increment = NoteToFrequency(carrier_note) * 0.5f;
  const float increment_1 = NoteToFrequency(note_1) * 0.5f;
  const float increment_2 = NoteToFrequency(note_2) * 0.5f;
  const float carrier_frequency = increment * 2.0f;
  const float ratio_1 = increment_1 /
      (increment > 1.0e-9f ? increment : 1.0e-9f);
  const float ratio_2 = increment_2 /
      (increment > 1.0e-9f ? increment : 1.0e-9f);

  const float depth = parameters.morph;
  const float drive = ApplyMacro(1.0f, 0.5f, 4.0f, parameters.macro);
  // At MORPH 0 the modulators contribute nothing, so the engine is one sine
  // and there is no reason to pay for four table reads per output sample.
  const bool modulated = depth > 0.001f;

  const bool stereo = PLAITS_STEREO_RING_MOD && parameters.stereo;

  for (size_t i = 0; i < size; ++i) {
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    const float root_offset = parameters.frequency_offset
        ? parameters.frequency_offset[i]
        : 0.0f;
#else
    const float root_offset = 0.0f;
#endif
    float carrier_step = (carrier_frequency + root_offset) * 0.5f;
    float modulator_step_1 = increment_1 + root_offset * 0.5f * ratio_1;
    float modulator_step_2 = increment_2 + root_offset * 0.5f * ratio_2;
    CONSTRAIN(carrier_step, -0.5f, 0.499999f);
    CONSTRAIN(modulator_step_1, -0.5f, 0.499999f);
    CONSTRAIN(modulator_step_2, -0.5f, 0.499999f);
    for (int s = 0; s < 2; ++s) {
      phase_ += carrier_step;
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      } else if (phase_ < 0.0f) {
        phase_ += 1.0f;
      }
      modulator_phase_ += modulator_step_1;
      if (modulator_phase_ >= 1.0f) {
        modulator_phase_ -= 1.0f;
      } else if (modulator_phase_ < 0.0f) {
        modulator_phase_ += 1.0f;
      }
      modulator_phase_2_ += modulator_step_2;
      if (modulator_phase_2_ >= 1.0f) {
        modulator_phase_2_ -= 1.0f;
      } else if (modulator_phase_2_ < 0.0f) {
        modulator_phase_2_ += 1.0f;
      }

      const float carrier = SineNoWrap(phase_);
      float ring = carrier;
      float ring_aux = carrier;
      if (modulated) {
        const float m1 = 1.0f + depth * (SineNoWrap(modulator_phase_) - 1.0f);
        const float m2 = 1.0f + depth * (SineNoWrap(modulator_phase_2_) - 1.0f);
        ring_aux = carrier * m1;
        ring = ring_aux * m2;
      }

      history_position_ = (history_position_ + 1) & 15;
      history_[history_position_] = \
          Overdrive(kRingModPreShaperScale * drive * ring);
      if (stereo) {
        history_aux_[history_position_] = \
            Overdrive(kRingModPreShaperScale * drive * ring_aux);
      }
    }

    out[i] = Decimate(history_);
    if (stereo) {
      aux[i] = Decimate(history_aux_);
    } else {
      // Mono AUX: modulator 1 alone, at FULL level regardless of MORPH. The
      // depth-scaled version would go silent at MORPH 0 -- exactly where OUT
      // is a bare carrier and the modulator is the thing you would reach for.
      // (Same call sub-oscillator makes about its sub.)
      //
      // Every increment here is clamped to kRingModMaxPitch, 13.29 kHz, so
      // this sine is comfortably under Nyquist at the 48 kHz output rate and
      // needs neither the shaper nor the halfband. That is what makes the mono
      // path much cheaper than the stereo one rather than merely different.
      aux[i] = kRingModAuxGain * SineNoWrap(modulator_phase_);
    }
  }
}

}  // namespace plaits
