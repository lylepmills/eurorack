// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' TOY* model: a bit-mangled phase ramp through a sample-and-hold
// clock, 4x oversampled and decimated.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderToy. Nothing in
// the stock palette does sample-rate decimation of a mangled ramp -- chiptune
// is NES square/triangle plus an arpeggiator, tapfield is a Galois LFSR
// wavefield, rulefield is a cellular automaton.
//
// NOTE ON THE BRAIDS CONTROLS: Braids' TIMBRE sets the DECIMATION COUNT
// (512 - (parameter_[0] >> 6)), i.e. the sample-and-hold rate. It is not a
// bit-depth control -- the held sample is a uint8 either way. Braids' COLOR
// carries the mangle operand.
//
// OUT: the decimated stream. AUX (mono): the same stream sampled once per
// output with no reconstruction filter, so it keeps the aliasing the
// downsampler removes. In stereo OUT/AUX become L/R and the right channel
// runs a second hold clock 2.93% fast.
//
// NOTE ON THE NEW MACROS: MACRO's Braids-reproducing position is the usual
// detent, 0.5 -- ApplyMacro returns the stock value exactly there and
// static_cast<int>(x * 0.5f) == x >> 1 for every x in 0..127. MORPH's is 0.0,
// NOT 0.5: Braids' hold clock is always free-running, and the crossfade below
// collapses to it only at morph == 0. tests/ab.json holds them there.
//
// Declared deviations from Braids:
//   - the decimator is Plaits' 8-tap overlap-add lut_4x_downsampler_fir
//     rather than Braids' 4-tap non-overlapping {10530,14751,16384,14751}.
//     The Plaits kernel has unity DC gain while Braids' sums to 0.861 of full
//     scale after its >>8 and DC offset, so the port runs +1.30 dB hotter.
//     Measured 2026-07-29 at +1.24 to +1.29 dB across every ab.json case once
//     the DC blocker below is accounted for; the registered outGain of 0.85
//     (-1.41 dB) absorbs it to within 0.1 dB. (The +1.2 dB previously quoted
//     here was the DC-blocker-contaminated figure.)
//   - the port oversamples 4x from 48 kHz (192 kHz internal) where Braids
//     oversamples 4x from 96 kHz (384 kHz). The hold count is halved to
//     match, so the slowest clock is 750 Hz on both. It matches ONLY there:
//     the hold period is an integer tap count on both sides, so at half the
//     tap rate the port can realise only Braids' EVEN counts and reads
//     progressively low above TIMBRE 0 -- -0.4% at 0.5, -0.8% at 0.75, -3.7%
//     at 0.9. See KNOWN DEFECTS #3.
//
// KNOWN DEFECTS (found by the 2026-07-29 audit; NOT fixed, because the DSP is
// shipped and a change moves the package digest -- Lyle's call, not a
// maintenance edit). Each is reproduced by tests/ab.json:
//   1. COLOR quantiser. Braids takes x = parameter_[1] >> 8, i.e.
//      floor(p2 * 32767) >> 8; the port takes uint8(harmonics * 127.0f).
//      These disagree by 1 on 49.8% of the knob. Because `& (~x)` makes x a
//      bit MASK, an off-by-one at a power-of-two boundary is a large timbral
//      jump: at p2 = 0.502 Braids gets x=64 and the port x=63, and the A/B
//      goes from 0.44 to 1.26 dB spectral delta and +1.19 to +1.86 dB AC RMS.
//      The faithful form is min(127, int(harmonics * 128.0f)).
//   2. Undeclared DC blocker. Render() runs a one-pole high-pass (a = 0.9975,
//      corner ~19 Hz) on both outputs. Braids' RenderToy has no high-pass at
//      all -- it removes DC with the fixed kFIR4DcOffset, which is exact
//      because the mangled ramp's mean stays at 127..127.5 for every x. The
//      port therefore loses ~1.3 dB at a 32.7 Hz fundamental: the ab.json
//      low-note-crush case reads +0.35 dB AC RMS against the +1.19 dB of
//      every other case, and DC-blocking the Braids reference restores it to
//      +1.29 dB and drops that case's spectral delta from 0.81 to 0.44 dB.
//   3. Hold-clock rate. Following from the halved internal rate above, at
//      TIMBRE 0.5 Braids' clock is 384000/257 = 1494.16 Hz and the port's is
//      192000/129 = 1488.37 Hz, -6.72 cents. The A/B measures -6.6 to -6.7 c
//      on three independent COLOR settings and tracks the prediction at
//      TIMBRE 0.25 and 0.375 as well. Inherent to running the oversampler at
//      192 kHz; a fractional-phase hold clock would remove it.
//   4. Reset() leaves held_sample_ = 128 (silence). Braids' Init() memsets
//      state_, so its held_sample starts at 0 and RenderToy emits ~64 output
//      samples of -0.861 full-scale DC before the first hold. The port emits
//      exactly 0.0 there. One-shot, at engine load only -- Braids does not
//      reset held_sample on sync and neither does the port on trigger.

#ifndef PLAITS_DSP_ENGINE2_TOY_ENGINE_H_
#define PLAITS_DSP_ENGINE2_TOY_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids' decimation_count spans 1..512 at its 384 kHz internal rate. Halved
// here for the port's 192 kHz internal rate, so both bottom out at 750 Hz.
const float kToyMaxDecimation = 256.0f;

// The right channel's hold clock runs slightly fast so the two sides drift
// against each other. At MORPH 1 -- where the point is that the crush LOCKS
// to the note -- this deliberately unlocks one side; the width is worth it
// and mono users are unaffected.
const float kToyStereoClockRatio = 1.0293f;

class ToyEngine : public Engine {
 public:
  ToyEngine() { }
  ~ToyEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool linear_tzfm_capable() const { return true; }
  virtual bool stereo_capable() const { return PLAITS_STEREO_TOY; }

 private:
  float phase_;
  float ramp_increment_;

  float decimation_counter_;
  uint8_t held_sample_;

  float decimation_counter_r_;
  uint8_t held_sample_r_;

  float downsampler_state_;
  float downsampler_state_r_;

  float dc_input_;
  float dc_output_;
  float dc_input_aux_;
  float dc_output_aux_;

  DISALLOW_COPY_AND_ASSIGN(ToyEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_TOY_ENGINE_H_
