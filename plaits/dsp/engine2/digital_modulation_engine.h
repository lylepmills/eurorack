// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' QPSK model: a carrier driven by a framed packet of dibits through a
// four-point constellation.
//
// The algorithm is Emilie Gillet's
// DigitalOscillator::RenderDigitalModulation. Nothing in the stock palette
// does packet-framed I/Q.
//
// Braids has two knobs here and neither is a frame control: parameter_[0] is
// the symbol rate and parameter_[1] the payload byte, and the packet is
// welded to 1,088 symbols with its preamble and two sync words at 32, 48 and
// 64. HARMONICS opens that frame up and MORPH shapes the symbol transitions;
// both are new.
//
// FRAME LENGTH IS ON HARMONICS DELIBERATELY. Put it on MACRO and the detent
// -- which is where the fourth macro sits unless the frequency knob is
// reassigned -- pins the stock 1,088-symbol frame. At MIDI 36 with TIMBRE
// down the symbol rate is about 5 Hz, so that frame runs about 211 seconds
// and the control is inert for the whole header. A knob that does nothing at
// its default position is a defect, not a testing inconvenience.
//
// OUT: the modulated carrier, zero-mean by construction. AUX: the symbol
// staircase, which is a modulation source rather than a second voice -- at
// MIDI 36 with TIMBRE down it is a ~5 Hz stepped LFO, and only becomes a
// voice at high TIMBRE and mid-to-high pitch. It sits at +1.0 for the whole
// preamble, so it carries a DC blocker and a NEGATIVE gain (R1).
//
// Declared deviation: Braids reseeds nothing on sync but its own symbol
// count, so the carrier phase runs free across a trigger. Kept.

#ifndef PLAITS_DSP_ENGINE2_DIGITAL_MODULATION_ENGINE_H_
#define PLAITS_DSP_ENGINE2_DIGITAL_MODULATION_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// The constellation radius, 23100/32768. Two of these in quadrature give a
// carrier peak of R*sqrt(2) = 0.997.
const float kDigitalModulationRadius = 23100.0f / 32768.0f;

// Braids' packet: preamble to 32, sync words to 48 and 64, payload to 1,088.
const float kDigitalModulationStockFrame = 1088.0f;
const float kDigitalModulationMinFrame = 32.0f;
const float kDigitalModulationPreamble = 32.0f;
const float kDigitalModulationSyncA = 48.0f;
const float kDigitalModulationSyncB = 64.0f;

// `pitch_ - 1536 + ((parameter_[0] - 32767) >> 3)`: an octave down, then up
// to another 32 semitones below that.
const float kDigitalModulationSymbolOffset = -12.0f;
const float kDigitalModulationSymbolRange = 32.0f;

// Braids' payload one-pole, `(state * 3 + parameter_[1]) >> 2`.
const float kDigitalModulationPayloadPole = 0.75f;
const float kDigitalModulationStockPayload = 128.0f;

const float kDigitalModulationDcPole = 0.999f;

class DigitalModulationEngine : public Engine {
 public:
  DigitalModulationEngine() { }
  ~DigitalModulationEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: I on one side, Q on the other. Note the two are largely a
  // quadrature pair rather than different waveforms -- heard as phasey width,
  // decorrelated only by the per-symbol sign flips -- and each channel peaks
  // at 0.705 against the mono 0.997, so stereo is ~3 dB quieter (R13).
  virtual bool stereo_capable() const { return true; }

 private:
  float phase_;
  float symbol_phase_;
  float payload_filter_;
  int symbol_count_;
  int data_byte_;

  float shaped_i_;
  float shaped_q_;

  float dc_aux_in_;
  float dc_aux_out_;

  DISALLOW_COPY_AND_ASSIGN(DigitalModulationEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_DIGITAL_MODULATION_ENGINE_H_
