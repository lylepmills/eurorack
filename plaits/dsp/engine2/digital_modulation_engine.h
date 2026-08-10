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
// PAYLOAD IS AN INTEGER PIPELINE, END TO END. MACRO is Braids' COLOR knob and
// it enters the packet exactly as the module delivers it: an int16 0..32767
// (braids.cc:219-224), chased by an int32 one-pole `(state * 3 + knob) >> 2`
// and read out as `state >> 7` (digital_oscillator.cc:2217-2219). Both shifts
// FLOOR. Compute the byte as a float instead and a quarter of the knob's
// travel transmits a different byte -- and the byte IS the dibit sequence, so
// 0x80 is [0,0,0,2] where 0x7f is [3,3,3,1]. The >>2 floor is also
// ASYMMETRIC, which a float pole is not: x, x-1, x-2 and x-3 are ALL fixed
// points of (state * 3 + knob) >> 2, so a filter climbing from below parks
// exactly three LSB short of the knob and one settling down onto it from above
// parks exactly on it. Measured at every MACRO position. The detent is the
// module's noon, 16383 >> 7 = 127, not a rounded 128.
//
// FRAME LENGTH IS ON HARMONICS DELIBERATELY. Put it on MACRO and the detent
// -- which is where the fourth macro sits unless the frequency knob is
// reassigned -- pins the stock 1,088-symbol frame. At MIDI 36 with TIMBRE
// down the symbol rate is about 5 Hz, so that frame runs about 211 seconds
// and the control is inert for the whole header. A knob that does nothing at
// its default position is a defect, not a testing inconvenience.
//
// OUT: the modulated carrier. NOT strictly zero-mean -- wav_sine's own +127
// pedestal (see the BraidsSine note below) rides through the constellation
// multiply and, whenever the dibit holds still (the preamble's constant
// 0x00, for instance), sums to a real DC offset of ped*2R = 127/32768 *
// 2*23100/32768 =~ 0.0055, i.e. -45.3 dBFS -- exactly as the module's own
// un-DC-blocked OUT does. This is Braids' own artifact, not a port defect.
//
// AUX (mono): the symbol staircase, which is a modulation source rather than a
// second voice -- at MIDI 36 with TIMBRE down it is a ~5 Hz stepped LFO, and
// only becomes a voice at high TIMBRE and mid-to-high pitch. Nothing in the
// stock palette emits a control signal, so this is if anything MORE distinct
// than the usual AUX. It sits at +1.0 for the whole preamble, so it carries a
// DC blocker and a NEGATIVE gain (R1).
//
// In stereo OUT/AUX become L/R and carry the two QUADRATURE components: I on
// the left, Q on the right. That is a decomposition rather than a second
// render -- both terms are already computed for the mono sum, so L + R
// reproduces the mono output EXACTLY and the pair is perfectly mono-compatible.
// Each channel peaks at the constellation radius against the mono R*sqrt(2),
// so stereo is 3 dB down per channel; two decorrelated channels at -3 dB carry
// the same power as one at 0, and a make-up would break the identity, so none
// is applied. The staircase is NOT sent to a channel here: a DC-blocked
// stepped LFO hard right is a poor right channel, which is exactly the split
// Pattern B exists to make.
//
// Declared deviation: Braids reseeds nothing on sync but its own symbol
// count, so the carrier phase, the payload filter and the byte in flight all
// run free across a trigger. Kept -- the port now resets the symbol count and
// NOTHING ELSE, so the three symbols after a strike are the shifted remnant of
// the byte that was being transmitted and the preamble's 0x00 latches at
// count 4, exactly as on the module.
//
// FIXED (was a declared deviation): BraidsSine used to be a zero-mean unit
// -cos, where Braids reads its 257-entry wav_sine table, which is exactly
// 32639 * -cos(2*pi*x) + 127 (braids/resources.cc; min -32512, max 32766,
// fit by least squares against every one of the 257 entries, max residual
// 1.68 LSB -- the table's own int16 quantization noise, not a modeling
// error). This sine feeds a plain MULTIPLY-and-sum (shaped_i_ * in_phase +
// shaped_q_ * quadrature), never a wavefolder/waveshaper/clipper/comparator
// or a feedback loop, so the amplitude and pedestal terms looked like they
// should be inaudible on the usual "linear path" reasoning -- except
// shaped_i_/shaped_q_ are not constant, they step at the symbol rate, so a
// constant pedestal times a stepping amplitude is NOT constant: it is a
// symbol-rate square wave Braids actually transmits and the port had no
// trace of. Both terms are real, and both were measurable:
// (1) The port's carrier was 0.0343 dB hot -- 20*log10(32768/32639) --
// which read as a uniform +0.03/+0.04 dB AC RMS on every one of this
// engine's eight A/B cases, before and after the payload fix.
// (2) wav_sine's +127 offset rides through the constellation multiply as
// (23100 * 127) >> 15 = 89 per quadrature component, sign following the
// dibit -- a +-178/32768 = -45.3 dBFS symbol-rate square wave Braids' OUT
// carries and the port lacked.
// Reproducing the exact table (32639 * -cos + 127, not unit-amplitude
// zero-mean -cos) fixed both: AC RMS moved from the uniform +0.03/+0.04 dB
// to +-0.01 dB on every case (ab_engine.py --bands,
// packages/mutable-instruments/digital-modulation), and the low-frequency
// bands the pedestal square-wave lives in tightened on most cases where it
// carries any real energy -- e.g. strike-restart's 20-40 Hz band -1.38 dB ->
// -0.12 dB, its overall spectrum error 0.19 dB -> 0.17 dB; payload-low's
// 20-40 Hz -0.29 dB -> -0.08 dB; timbre-high's 20-40 Hz -0.21 dB -> -0.06 dB.
// (pitch-check's 20-40 Hz band moved the other way, -2.57 dB -> +0.92 dB,
// but that band carries 0.1% of that case's energy and its overall spectrum
// error is unchanged at 0.10 dB -- noise, not a regression.) Kept: this was
// measured to matter, not assumed either way. No tolerance was widened to
// land it -- every case tightened or was already comfortably inside its
// existing bound.
//
// HOW THIS SOUNDS DIFFERENT FROM THE SHIPPED 1.0.0 (three audible changes):
//   * A freshly selected voice used to open its payload from byte 128 and
//     decay toward the knob; it now climbs from 0 like the module. At MACRO
//     down that removes an entire spurious modulated passage -- the shipped
//     build put +7 to +11 dB of PSK sideband energy above 320 Hz into a region
//     where the module transmits a constant unmodulated dibit. Slow symbol
//     rates and long frames hold the difference for seconds.
//   * The payload byte moves by one LSB over about a quarter of MACRO's
//     travel, and one LSB of BYTE is a different four-symbol pattern, not a
//     rounding. The detent moves 128 -> 127, which is a 90-degree
//     constellation rotation and so a fixed carrier phase offset rather than a
//     timbre change; positions away from the powers of two genuinely re-key.
//   * A retriggered packet no longer forces dibit 0 into the three symbols
//     between the strike and the preamble; they carry the tail of the byte
//     that was interrupted, so the restart has up to three extra constellation
//     jumps in it. Every dibit is a full-amplitude carrier phase, so this is a
//     change of grit, not of level, and it is CONDITIONAL: it can only differ
//     when the packet has advanced past symbol 32 since the last strike, i.e.
//     when symbol rate / trigger rate > 32. Below that the strike always lands
//     inside the preamble, where the byte in flight is already 0x00 and the
//     old and new code emit the same thing. At the settings this engine's own
//     copy calls typical -- MIDI 36 with TIMBRE down, ~5 Hz symbols -- any
//     musical trigger rate is far under that ratio, so most patches will not
//     hear this at all.

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
// 12 * log2(1088/32): the frame span expressed in semitones so the mapping
// can use SemitonesToRatio rather than a libm exponential.
const float kDigitalModulationFrameSpan = 61.05f;
const float kDigitalModulationPreamble = 32.0f;
const float kDigitalModulationSyncA = 48.0f;
const float kDigitalModulationSyncB = 64.0f;

// `pitch_ - 1536 + ((parameter_[0] - 32767) >> 3)`: an octave down, then up
// to another 32 semitones below that.
const float kDigitalModulationSymbolOffset = -12.0f;
const float kDigitalModulationSymbolRange = 32.0f;

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
  virtual bool linear_tzfm_capable() const { return true; }
  virtual bool fast_fm_capable() const { return true; }
  // Pattern B: mono AUX is the symbol staircase, a control signal; the stereo
  // branch drops it and splits OUT into its I and Q components instead. The two
  // are a quadrature pair rather than different waveforms -- heard as phasey
  // width, decorrelated by the per-symbol sign flips -- and each channel peaks
  // at 0.705 against the mono 0.997, so stereo is ~3 dB quieter per channel
  // (R13).
  virtual bool stereo_capable() const { return PLAITS_STEREO_DIGITAL_MODULATION; }

 private:
  float phase_;
  float symbol_phase_;
  // Braids' DigitalModulationState, in its own types: the payload filter is an
  // int32 accumulator in knob units (0..32767), not a float byte, and the byte
  // in flight is a uint8 that shifts right two bits per symbol.
  int32_t payload_filter_;
  int symbol_count_;
  uint8_t data_byte_;

  float shaped_i_;
  float shaped_q_;

  float dc_aux_in_;
  float dc_aux_out_;

  DISALLOW_COPY_AND_ASSIGN(DigitalModulationEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_DIGITAL_MODULATION_ENGINE_H_
