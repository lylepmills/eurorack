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
// Declared deviations from Braids:
//   - the decimator is Plaits' 8-tap overlap-add lut_4x_downsampler_fir
//     rather than Braids' 4-tap non-overlapping {10530,14751,16384,14751}.
//     Braids' kernel sums to 0.861 of full scale after its >>8 and DC offset,
//     so the port runs +1.3 dB hotter -- the measured gap is +1.2 dB, and the
//     registered gain absorbs it.
//   - the port oversamples 4x from 48 kHz (192 kHz internal) where Braids
//     oversamples 4x from 96 kHz (384 kHz). The hold count is halved to
//     match, so the slowest clock is 750 Hz on both.

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
