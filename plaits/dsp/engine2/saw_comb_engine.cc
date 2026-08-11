// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' saw-into-comb hybrid.

#include "plaits/dsp/engine2/saw_comb_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"

#include "plaits/build_config.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids warps the resonance knob through ws_moderate_overdrive
// (digital_oscillator.cc:261-263). That table is NOT tanh(2x)/tanh(2): its
// generator (braids/resources/waveshapers.py:26-40,62) samples tanh(2x) at
// 257 points over [-1, 1], sets the LAST POINT EQUAL TO THE ONE BEFORE IT,
// subtracts the mean, and scales to +-32766. Two consequences a closed-form
// SoftLimit misses, both audible:
//
//   * the curve is FLAT above x = 255/128 - 1, so the module's maximum
//     feedback is the truncated 32728/32768 = 0.998779 and its loop DECAYS;
//   * the mean-centring and max-normalisation, not tanh(2), set the scale.
//
// Reproducing the generator instead of the Pade form: verified against the
// real ws_moderate_overdrive through Braids' own Interpolate88, worst case
// 4.05 LSB of 32768 over all 32,768 knob positions -- against 199 LSB for
// the normalised SoftLimit this replaces. tanhf runs once per block, not per
// sample.
inline float WarpResonance(float x) {
  CONSTRAIN(x, -1.0f, kSawCombWarpTop);
  return kSawCombWarpScale * (tanhf(2.0f * x) - kSawCombWarpMean);
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
  // ENGINE-SWITCH reset only -- this is NOT called on a trigger, because a
  // strike does nothing on the module (MacroOscillator::Strike reaches
  // DigitalOscillator::Strike, which only sets strike_, and RenderComb never
  // reads it). It IS required here: another engine's buffers alias this line
  // at the same addresses, so a new engine must not read the previous one's
  // memory (R15).
  if (line_) {
    for (size_t i = 0; i < kSawCombDelaySize; ++i) {
      line_[i] = 0;
    }
  }
  exciter_.Init();
  write_pointer_ = 0;
  // Braids' Init() memsets state_, so its comb-pitch smoother starts at 0 --
  // MIDI 0 -- and glides UP into the note. Seed it the same way; a fixed
  // MIDI 48 seed put a comb glide at the start of every note that the module
  // has no counterpart for.
  filtered_pitch_ = 0;
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

  const float frequency = NoteToFrequency(parameters.note);

  // TIMBRE detunes the comb up to 64 semitones either side of the note.
  // Braids smooths this PITCH, not the delay, to avoid clicks -- and it does
  // so with an INTEGER recursion in 1/128-semitone units
  // (digital_oscillator.cc:248-252), reproduced here verbatim:
  //
  //     pitch          = pitch_ + ((parameter_[0] - 16384) >> 1)
  //     filtered_pitch = (15 * filtered_pitch + pitch) >> 4
  //
  // `>> 4` FLOORS, so every value in [p-15, p] is a fixed point and the
  // filter STALLS 15 LSB = 11.72 cents FLAT of its target when it rises into
  // it, then stays there. A float pole converges exactly and so ran the
  // comb 11.72 cents sharp of the module for the life of every note.
  //
  // The note the offset applies to is CLAMPED first, because Braids clamps
  // its own pitch_ to [0, kHighestNote] in DigitalOscillator::Render
  // (digital_oscillator.cc:120-124) BEFORE dispatching to RenderComb. Plaits'
  // note reaches -119 (voice.cc:265) where the module's is pinned at MIDI 0,
  // so without this the comb runs octaves flat of the module's under a
  // downward pitch CV.
  int32_t note_pitch = static_cast<int32_t>(parameters.note * 128.0f);
  CONSTRAIN(note_pitch, kSawCombNoteFloor, kSawCombNoteCeiling);
  const int32_t target_pitch = note_pitch +
      ((static_cast<int32_t>(parameters.timbre * 32767.0f) - 16384) >> 1);
  // One Braids block is 24 samples at 96 kHz; one host block is kBlockSize
  // at 48 kHz. At the stock kBlockSize of 12 that is exactly one update.
  size_t updates = size / kBlockSize;
  if (!updates) {
    updates = 1;
  }
  while (updates--) {
    filtered_pitch_ = (15 * filtered_pitch_ + target_pitch) >> 4;
  }

  // ComputeDelay's own ceiling (digital_oscillator.cc:73-76):
  // kHighestNote - kOctave = 140*128 - 12*128 = MIDI 128 = 13.29 kHz. Above
  // played note ~64 this is what stops the top of TIMBRE.
  int32_t comb_pitch = filtered_pitch_;
  if (comb_pitch >= kSawCombPitchCeiling) {
    comb_pitch = kSawCombPitchCeiling;
  }

  // The ONLY correct delay form: NoteToFrequency returns cycles per sample,
  // so the period is its reciprocal. Multiplying by a sample rate is wrong by
  // fs squared -- about 9.5e6 samples (R6). The upper clamp is the WHOLE
  // line: below MIDI -16 Braids' ComputeDelay shifts by a negative count and
  // returns 0, which its own `delay_ptr + 2*kCombDelayLength - 0` then reads
  // as a full-line 8,192-tap delay. 4,096 taps at 48 kHz is the same 85.33 ms.
  float delay = 1.0f / max(1e-7f,
      NoteToFrequency(static_cast<float>(comb_pitch) * (1.0f / 128.0f)));
  CONSTRAIN(delay, 2.0f, static_cast<float>(kSawCombDelaySize));

  // AUX taps a fifth BELOW: 1.5x the delay is 2/3 the frequency. Once 1.5x
  // would clamp, INVERT the ratio -- putting that tap a fifth ABOVE -- rather
  // than clamping both taps to the same value, which would collapse the
  // stereo image to mono at the bottom of TIMBRE.
  float delay_aux = delay * 1.5f;
  if (delay_aux > static_cast<float>(kSawCombDelaySize)) {
    delay_aux = delay / 1.5f;
  }
  CONSTRAIN(delay_aux, 2.0f, static_cast<float>(kSawCombDelaySize));

  // HARMONICS is bipolar and warped, exactly as Braids warps its knob --
  // including the truncated table top that keeps the loop sub-unity.
  float feedback = WarpResonance(2.0f * parameters.harmonics - 1.0f);

  // MACRO tilts the loop: damped below noon, bright above.
  const float tilt = ApplyMacro(
      0.0f, kSawCombTilt, -kSawCombTilt, parameters.macro);

  // The shelf's HF gain is (1 - 0.788 * tilt), so on the bright side the loop
  // is amplified at HF and a fixed pre-scale does not cancel it: the spec's
  // 1/(1 + 0.6*max(-tilt,0)) leaves a net HF loop gain of 1.083 and the comb
  // self-oscillates well below the HARMONICS setting that should do it.
  // Divide by the ACTUAL shelf peak instead. H(z) = (1 - tilt) + tilt*L(z)
  // is exactly 1 at DC for any tilt and 1 - 0.788*tilt at Nyquist, so its
  // peak is 1 on the damping half and 1 + 0.788*|tilt| on the bright half --
  // which this cancels. The loop's peak gain is therefore |feedback|, and
  // WarpResonance caps that at 0.998779: no further bound is needed, and the
  // one that used to sit here assumed a peak of 1 + |tilt| and so cut 4 dB
  // of resonance range off the damping half for no stability reason.
  feedback *= 1.0f / (1.0f - kSawCombShelfHf * min(tilt, 0.0f));

  // MORPH morphs the exciter from a saw to a narrowing pulse. waveshape 0.5
  // is the saw and 1.0 the square (0.0 would be a triangle).
  const float waveshape = 0.5f + 0.5f * parameters.morph;
  const float pw = 0.5f - 0.25f * parameters.morph;

  // VariableShapeOscillator::Render WRITES rather than accumulates, so the
  // exciter needs its own scratch. Sized off kMaxBlockSize.
  float exciter[kMaxBlockSize];
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  if (parameters.frequency_offset) {
    exciter_.RenderLinearFm(
        frequency, pw, waveshape, parameters.frequency_offset, exciter, size);
  } else {
    exciter_.Render(frequency, pw, waveshape, exciter, size);
  }
#else
  exciter_.Render(frequency, pw, waveshape, exciter, size);
#endif

  for (size_t i = 0; i < size; ++i) {
    const float in = exciter[i];
    float sample_delay = delay;
    float sample_delay_aux = delay_aux;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      float root_frequency = frequency + parameters.frequency_offset[i];
      if (root_frequency < 1.0e-7f) {
        root_frequency = 1.0e-7f;
      }
      const float inverse_ratio = frequency / root_frequency;
      sample_delay *= inverse_ratio;
      sample_delay_aux *= inverse_ratio;
      CONSTRAIN(sample_delay, 2.0f, static_cast<float>(kSawCombDelaySize));
      CONSTRAIN(
          sample_delay_aux, 2.0f, static_cast<float>(kSawCombDelaySize));
    }
#endif
    const float delayed = ReadLine(line_, write_pointer_, sample_delay);
    const float delayed_aux =
        ReadLine(line_, write_pointer_, sample_delay_aux);

    // In-loop shelf, then Braids' write-back: resonance times the echo plus
    // half the exciter, clipped.
    loop_lp_ += (delayed - loop_lp_) * kSawCombShelfPole;
    const float shaped = delayed + (loop_lp_ - delayed) * tilt;
    float written = (shaped * feedback + 0.5f * in) * 32768.0f;
    // Braids' `CLIP(feedback)` -- the int16 rails, not +-1.0.
    CONSTRAIN(written, -32768.0f, 32767.0f);
    // ROUND, and scale by 32768 to match the 1/32768 ReadLine uses, so the
    // store/load round trip has gain exactly 1 and no bias -- as Braids' does,
    // its line holding exact integers. `written * 32767.0f` with a truncating
    // cast instead put a 3.1e-5 per-pass loss (the 32767/32768 scale) and a
    // ~0.5 LSB bias toward zero in the loop, of the same order as the 6.1e-5
    // the maximum feedback leaves. MEASURED: worth under 0.05 dB on every A/B
    // case, including the two extreme-feedback ones -- it is kept because it
    // is what the module does, not because it moved a number.
    line_[write_pointer_ & (kSawCombDelaySize - 1)] = static_cast<int16_t>(
        written + (written >= 0.0f ? 0.5f : -0.5f));
    ++write_pointer_;

    // Braids' `out = (in + (delayed_sample << 1)) >> 1` and its HARD
    // `CLIP(out)` (digital_oscillator.cc:277-278). Not SoftClip: SoftLimit
    // attenuates everywhere below the rail, not only at it.
    float main_out = 0.5f * in + delayed;
    CONSTRAIN(main_out, -1.0f, 1.0f);
    out[i] = main_out;
    float aux_out = 0.5f * in + delayed_aux;
    CONSTRAIN(aux_out, -1.0f, 1.0f);
    aux[i] = aux_out;
  }
}

}  // namespace plaits
