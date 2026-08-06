// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' saw-into-comb hybrid: a band-limited exciter through a comb whose
// pitch is decoupled from the played note.
//
// The algorithm is Emilie Gillet's MacroOscillator::RenderSawComb -- an
// AnalogOscillator saw written into the buffer, then
// DigitalOscillator::RenderComb filtering it in place. RenderComb carries no
// `size -= 2`, so it is a 96 kHz algorithm; a 4,096-tap line at 48 kHz
// reproduces Braids' 8,192 taps at 96 kHz -- 85.33 ms either way, since the
// bottom clamp now uses the whole line -- so the bottom of TIMBRE clamps
// below MIDI ~70 just as it does on hardware.
//
// Braids' block is 24 samples at 96 kHz and Plaits' kBlockSize is 12 at
// 48 kHz, so ONE host block IS one Braids block: every block-rate quantity
// below, in particular the comb-pitch smoother, transfers 1:1.
//
// What distinguishes this from reed-pipe and loopback, which are the two
// palette neighbours: the comb pitch is decoupled +-64 semitones from the
// note, the feedback is genuinely BIPOLAR, and the exciter is band-limited.
//
// MORPH morphs the exciter from saw to pulse and MACRO tilts the loop; both
// are new. At the MACRO detent the loop is flat, which is Braids. MORPH,
// which has no detent (SPEC R11), is Braids' OSC_SHAPE_SAW exciter at
// **MORPH 0**, not at noon: noon gives waveshape 0.75 and pw 0.375, a
// half-square at 37.5% duty that the module cannot make. Every A/B case
// therefore holds MORPH at 0.0 and MACRO at 0.5.
//
// AT HARMONICS NOON THE ENGINE IS NOT SILENT. The write-back is 0.5*in and
// the output is 0.5*dry + one echo -- a fully audible FIR comb. Resonance
// runs from inverted, through that, to ringing.
//
// DC, stated correctly: a comb attenuates DC by 1/(1+|g|) when the feedback
// is NEGATIVE and AMPLIFIES it by 1/(1-g) when positive, so DC parking is a
// POSITIVE-feedback hazard. The damping half of MACRO has unity gain at DC
// and so bounds nothing; only the in-loop clip does, exactly as in Braids.
//
// WHAT THIS PORT REPRODUCES RATHER THAN APPROXIMATES (correctness pass,
// 2026-07-29). Each item below was a DECLARED DEVIATION until that pass and
// is now gone from the DSP. Every one is the same failure: quantised
// arithmetic that a closed form got "right", and therefore got wrong.
//
//   1. THE COMB-PITCH SMOOTHER IS BRAIDS' INTEGER RECURSION, floor and all:
//      `filtered_pitch = (15 * filtered_pitch + pitch) >> 4`
//      (digital_oscillator.cc:250-252) on an int32 in 1/128-semitone units.
//      `>> 4` FLOORS, so every value in [p-15, p] is a fixed point: rising
//      from the 0 Init() memsets into state_.ffm.previous_sample, the filter
//      STALLS 15 LSB = 11.72 cents FLAT of the requested comb pitch and
//      stays there for the life of the note. Measured: exactly -15 LSB at
//      every A/B case pitch (falling toward a target, by contrast, lands
//      exact). A float pole converges exactly and so ran the port's comb
//      11.72 cents SHARP of the module, its delay 0.680% short -- 873.12
//      taps @96 kHz against the module's 879.05 at note 45 / TIMBRE noon.
//   1b. THE NOTE THE COMB OFFSETS FROM CARRIES BRAIDS' OWN CLAMP.
//      DigitalOscillator::Render pins pitch_ to [0, kHighestNote] =
//      [MIDI 0, MIDI 140] BEFORE dispatching to RenderComb
//      (digital_oscillator.cc:120-124). Plaits' note reaches -119
//      (voice.cc:265), where the module's is pinned at MIDI 0, so applying
//      TIMBRE's offset to the raw note put the comb up to an octave flat of
//      the module's under a downward pitch CV -- MIDI 51.99 against 63.99 at
//      note -12 / TIMBRE 1. Found by the verification pass; see the
//      low-note-clamp A/B case (0.54 -> 0.05 dB).
//   2. THE COMB PITCH CARRIES BRAIDS' CEILING. ComputeDelay opens with
//      `if (midi_pitch >= kHighestNote - kOctave) midi_pitch = kHighestNote
//      - kOctave` (digital_oscillator.cc:73-76); with kHighestNote = 140*128
//      and kOctave = 12*128 that pins the comb at 16384 = MIDI 128 =
//      13.29 kHz. Above played note ~64 the top of TIMBRE therefore stops
//      an octave lower than an unclamped port put it.
//   3. THE FEEDBACK WARP IS ws_moderate_overdrive'S OWN GENERATOR, not a
//      normalised SoftLimit. The table's last two points are EQUAL, so its
//      top is 32728/32768 = 0.998779 and the module's loop DECAYS, where the
//      Pade form reached exactly 1.0 and was marginally stable. Verified
//      against the real table: 4.05 LSB of 32768 worst case, against 199 LSB
//      for the form it replaces. See WarpResonance in the .cc.
//   4. THE OUTPUT CLIPPER IS HARD, as Braids' `CLIP(out)` is
//      (digital_oscillator.cc:278). SoftClip is SoftLimit below +-3 and so
//      attenuates everywhere, not only at the rail -- SoftLimit(1.0) = 0.778
//      -- which cost 1.25 dB of level on the stock case.
//   5. A TRIGGER DOES NOTHING, because it does nothing on the module:
//      MacroOscillator::Strike reaches DigitalOscillator::Strike, which only
//      sets strike_, and RenderComb never reads it. Reset() is now the
//      engine-switch reset ONLY -- it is still required there, because
//      another engine's buffers alias this line (R15) -- and the smoother is
//      seeded to Braids' own 0 rather than to a fixed MIDI 48, so a new note
//      no longer starts with a 36-semitone comb glide the module never has.
//
// DECLARED DEVIATIONS THAT REMAIN:
//
//   a. ComputeDelay's LUT is replaced by 1/NoteToFrequency. Checked against
//      the real lut_oscillator_delays at every A/B case pitch, the two agree
//      to within +0.0006% (0.01 cents), which is below the noise floor of
//      the comparison. The closed form does inherit Plaits'
//      kCorrectedSampleRate, so the comb sits the same +4.61 cents off
//      nominal as the played note does -- i.e. exactly in tune WITH the
//      note, which is what the ear tracks.
//   b. At the bottom of TIMBRE, Braids' ComputeDelay shifts by a NEGATIVE
//      count (num_shifts > 12, i.e. comb pitch below MIDI -16) and returns
//      0, which `offset = delay_ptr + 2*kCombDelayLength - 0` then reads as
//      a full-line 8,192-tap delay. That is undefined behaviour, reproduced
//      only in EFFECT: the port clamps to its own full line, 4,096 taps at
//      48 kHz, which is the same 85.33 ms.
//   c. MORPH and MACRO are not on the module at all; only their detent
//      positions (MORPH 0, MACRO 0.5) are A/B-able. The block-rate loop
//      bound that used to sit on the MACRO axis is gone: it assumed the
//      shelf peaks at 1 + |tilt|, but H(z) = (1 - tilt) + tilt*L(z) is
//      exactly 1 at DC for any tilt, so after the existing reciprocal HF
//      compensation the loop's peak gain is |feedback| on BOTH halves and
//      the warp's own 0.998779 ceiling is the bound.

#ifndef PLAITS_DSP_ENGINE2_SAW_COMB_ENGINE_H_
#define PLAITS_DSP_ENGINE2_SAW_COMB_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/variable_shape_oscillator.h"

namespace plaits {

// 4,096 at 48 kHz == Braids' 8,192 at 96 kHz: 85.33 ms, floor 11.72 Hz.
const size_t kSawCombDelaySize = 4096;

// ComputeDelay's ceiling, kHighestNote - kOctave = 140*128 - 12*128, in the
// same 1/128-semitone units the smoother runs in: MIDI 128 = 13.29 kHz.
const int32_t kSawCombPitchCeiling = 16384;

// DigitalOscillator::Render clamps its OWN pitch_ to [0, kHighestNote] =
// [MIDI 0, MIDI 140] BEFORE dispatching to RenderComb
// (digital_oscillator.cc:120-124), so the note the comb offsets from is
// already clamped. Plaits' note runs to -119 (voice.cc:265), where an
// unclamped port would put the comb up to ten octaves below the module's.
const int32_t kSawCombNoteFloor = 0;
const int32_t kSawCombNoteCeiling = 17920;

// ws_moderate_overdrive, reproduced from its generator
// (braids/resources/waveshapers.py:26-40,62): tanh(2x) sampled at 257 points
// over [-1, 1] with the LAST TWO POINTS EQUAL, mean-centred, scaled to
// +-32766. kSawCombWarpTop is that duplicated point, 255/128 - 1, above
// which the module's interpolation is flat -- which is exactly why its
// maximum feedback is 32728/32768 = 0.998779 rather than 1.0.
const float kSawCombWarpMean = -0.000004360736f;
const float kSawCombWarpScale = 1.037256100043f;
const float kSawCombWarpTop = 0.9921875f;

// The in-loop shelf. ONE_POLE at 0.35 has a Nyquist response of
// 0.35/1.65 = 0.212, so `x + (lp - x) * tilt` has an HF gain of
// (1 - 0.788 * tilt) -- 1.473 at the bright extreme, which is why the
// feedback needs the reciprocal compensation rather than a fixed pre-scale.
const float kSawCombShelfPole = 0.35f;
const float kSawCombShelfHf = 0.788f;
const float kSawCombTilt = 0.6f;

class SawCombEngine : public Engine {
 public:
  SawCombEngine() { }
  ~SawCombEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: two comb taps a fifth apart on one line.
  virtual bool stereo_capable() const { return true; }
  virtual void HardSync() { exciter_.Init(); }

 private:
  VariableShapeOscillator exciter_;
  int16_t* line_;
  uint32_t write_pointer_;
  // Braids' state_.ffm.previous_sample: the comb pitch in 1/128-semitone
  // units, smoothed by an integer recursion whose `>> 4` floors.
  int32_t filtered_pitch_;
  float loop_lp_;

  DISALLOW_COPY_AND_ASSIGN(SawCombEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SAW_COMB_ENGINE_H_
