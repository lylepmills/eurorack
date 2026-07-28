// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' saw-into-comb hybrid.

#include "plaits/dsp/engine2/saw_comb_engine.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids warps the resonance knob through ws_moderate_overdrive, which is
// tanh(2x)/tanh(2); SoftLimit is the Pade form. Same substitution as ring-mod.
inline float WarpResonance(float x) {
  CONSTRAIN(x, -1.0f, 1.0f);
  return kSawCombShaperNorm * SoftLimit(2.0f * x);
}

inline float ReadLine(const int16_t* line, uint32_t write_pointer,
                      float delay) {
  const uint32_t integral = static_cast<uint32_t>(delay);
  const float fractional = delay - static_cast<float>(integral);
  const uint32_t offset = write_pointer + 2 * kSawCombDelaySize - integral;
  const float a = static_cast<float>(
      line[offset & (kSawCombDelaySize - 1)]);
  const float b = static_cast<float>(
      line[(offset - 1) & (kSawCombDelaySize - 1)]);
  return (a + (b - a) * fractional) * (1.0f / 32768.0f);
}

}  // namespace

void SawCombEngine::Init(BufferAllocator* allocator) {
  line_ = allocator->Allocate<int16_t>(kSawCombDelaySize);
  exciter_.Init();
  Reset();
}

void SawCombEngine::Reset() {
  // Another engine's buffers alias this line at the same addresses, so it
  // MUST be cleared on every engine switch (R15). The spec's own draft argued
  // the opposite -- that clearing 4,096 taps was a cycle spike for no musical
  // gain -- which would leave a new note reading the previous engine's memory.
  if (line_) {
    for (size_t i = 0; i < kSawCombDelaySize; ++i) {
      line_[i] = 0;
    }
  }
  exciter_.Init();
  write_pointer_ = 0;
  comb_pitch_ = 48.0f;
  loop_lp_ = 0.0f;
}

void SawCombEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (!line_) {
    for (size_t i = 0; i < size; ++i) {
      out[i] = 0.0f;
      aux[i] = 0.0f;
    }
    return;
  }

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Reset();
  }

  const float frequency = NoteToFrequency(parameters.note);

  // TIMBRE detunes the comb up to five octaves either side of the note.
  // Braids smooths this pitch, not the delay, to avoid clicks.
  const float target_pitch = parameters.note + \
      kSawCombPitchRange * (2.0f * parameters.timbre - 1.0f);
  comb_pitch_ += (target_pitch - comb_pitch_) * (1.0f - kSawCombPitchPole);

  // The ONLY correct delay form: NoteToFrequency returns cycles per sample,
  // so the period is its reciprocal. Multiplying by a sample rate is wrong by
  // fs squared -- about 9.5e6 samples (R6).
  float delay = 1.0f / max(1e-7f, NoteToFrequency(comb_pitch_));
  CONSTRAIN(delay, 2.0f, static_cast<float>(kSawCombDelaySize - 2));

  // AUX taps a fifth above. Once 1.5x would clamp, INVERT the ratio rather
  // than clamping both taps to the same value, which would collapse the
  // stereo image to mono at the bottom of TIMBRE.
  float delay_aux = delay * 1.5f;
  if (delay_aux > static_cast<float>(kSawCombDelaySize - 2)) {
    delay_aux = delay / 1.5f;
  }
  CONSTRAIN(delay_aux, 2.0f, static_cast<float>(kSawCombDelaySize - 2));

  // HARMONICS is bipolar and warped, exactly as Braids warps its knob.
  float feedback = WarpResonance(2.0f * parameters.harmonics - 1.0f);

  // MACRO tilts the loop: damped below noon, bright above.
  const float tilt = ApplyMacro(
      0.0f, kSawCombTilt, -kSawCombTilt, parameters.macro);

  // The shelf's HF gain is (1 - 0.788 * tilt), so on the bright side the loop
  // is amplified at HF and a fixed pre-scale does not cancel it: the spec's
  // 1/(1 + 0.6*max(-tilt,0)) leaves a net HF loop gain of 1.083 and the comb
  // self-oscillates well below the HARMONICS setting that should do it.
  // Divide by the ACTUAL shelf peak instead.
  feedback *= 1.0f / (1.0f - kSawCombShelfHf * min(tilt, 0.0f));
  // Belt and braces: bound the worst-case loop gain at block rate.
  const float worst_case = fabsf(feedback) * (1.0f + fabsf(tilt));
  if (worst_case > 1.0f) {
    feedback /= worst_case;
  }

  // MORPH morphs the exciter from a saw to a narrowing pulse. waveshape 0.5
  // is the saw and 1.0 the square (0.0 would be a triangle).
  const float waveshape = 0.5f + 0.5f * parameters.morph;
  const float pw = 0.5f - 0.25f * parameters.morph;

  // VariableShapeOscillator::Render WRITES rather than accumulates, so the
  // exciter needs its own scratch. Sized off kMaxBlockSize.
  float exciter[kMaxBlockSize];
  exciter_.Render(frequency, pw, waveshape, exciter, size);

  for (size_t i = 0; i < size; ++i) {
    const float in = exciter[i];
    const float delayed = ReadLine(line_, write_pointer_, delay);
    const float delayed_aux = ReadLine(line_, write_pointer_, delay_aux);

    // In-loop shelf, then Braids' write-back: resonance times the echo plus
    // half the exciter, clipped.
    loop_lp_ += (delayed - loop_lp_) * kSawCombShelfPole;
    const float shaped = delayed + (loop_lp_ - delayed) * tilt;
    float written = shaped * feedback + 0.5f * in;
    CONSTRAIN(written, -1.0f, 1.0f);
    line_[write_pointer_ & (kSawCombDelaySize - 1)] = \
        static_cast<int16_t>(written * 32767.0f);
    ++write_pointer_;

    // Braids' `(in + (delayed << 1)) >> 1`.
    out[i] = SoftClip(0.5f * in + delayed);
    aux[i] = SoftClip(0.5f * in + delayed_aux);
  }
}

}  // namespace plaits
