// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' three noise models -- NOIS (FILTERED_NOISE), TWNQ (TWIN_PEAKS_NOISE)
// and CLKN (CLOCKED_NOISE) -- in one slot, selected on HARMONICS the way
// z-filter merges the four digital filters.
//
// The algorithms are Emilie Gillet's DigitalOscillator::RenderFilteredNoise
// (digital_oscillator.cc:1833), RenderTwinPeaksNoise (:1878) and
// RenderClockedNoise (:1946).
//
// LINEAGE, and what the Plaits neighbour cannot reach. Plaits' filtered-noise
// engine (plaits/dsp/engine/noise_engine.cc) runs two clocked noise sources
// through an Svf, so on paper it covers all three of these. Three of the four
// things below it cannot do; the fourth it does better.
//   - Braids' CLKN RESEEDS its LCG on a period TIMBRE sets, so the random
//     sequence repeats and the model turns into a pitched buzz whose timbre is
//     frozen noise. Plaits' ClockedNoise is free-running; nothing in its noise
//     path loops.
//   - Braids' CLKN quantizes each held sample to 2..32 levels off COLOR.
//     Plaits' ClockedNoise is full-resolution.
//   - TWNQ's two resonators are int32 2-pole recursions reading
//     lut_resonator_coefficient, a table whose Q15 storage of 2*cos(w) runs
//     out of angular resolution at low notes, and whose excitation is scaled
//     by an INTEGER from lut_resonator_scale. Read back out of the committed
//     table, the pole sits +25.9 cents sharp at MIDI 45, +192 cents at MIDI 36
//     and +1641 cents at MIDI 12 -- entries 0..28 are all 65535, so the model
//     has a hard 42.20 Hz floor -- while the excitation collapses to eight
//     levels at MIDI 45, three at MIDI 36, and to digital silence below MIDI
//     32. None of that is reachable with an Svf and a float noise source, and
//     all of it is audible. It is also why this engine runs TWNQ in integers.
//   - NOIS is, honestly, a re-skin. White noise through one Chamberlin SVF
//     with an LP/BP/HP morph is what Plaits' filtered-noise already is, with a
//     better noise source and a Q control on top. What NOIS has that the
//     neighbour does not is the tanh(2x) overdrive stage both it and TWNQ end
//     in, and Braids' particular damp curve. That is the whole list, and the
//     README says so in those words.
//
// THE BRAIDS CONTROLS, with lines. Read the parameter INDEX, not the macro
// suffix -- parameter_[0] is TIMBRE, parameter_[1] is COLOR.
//   NOIS  parameter_[0] -> lut_svf_damp AND lut_svf_scale (:1838, :1839):
//                          resonance, and the gain correction derived from it.
//         parameter_[1] -> the LP/BP/HP morph gains (:1845-1853).
//         pitch_        -> lut_svf_cutoff (:1837).
//   TWNQ  parameter_[0] -> q = 65240 + (p0 >> 7) (:1888) AND
//                          makeup_gain = 8191 - (p0 >> 2) (:1904). One knob,
//                          two jobs: resonance up, output gain down.
//         parameter_[1] -> p2 = pitch_ + ((p1 - 16384) >> 1) (:1896), the
//                          second peak, +/-64 semitones around the note.
//   CLKN  parameter_[0] -> cycle_phase_increment (:1978), the LOOP LENGTH of
//                          the random sequence, in clock ticks.
//         parameter_[1] -> num_steps = 1 + (p1 >> 10) (:1982), the number of
//                          quantization levels, 2..32.
//         pitch_        -> the clock, at 8x the note (:1970-1975).
//
//   HARMONICS  Model      NOIS / TWNQ / CLKN.
//   TIMBRE     Colour     Braids' TIMBRE for the selected model.
//   MORPH      Shape      Braids' COLOR for the selected model.
//   MACRO      Drive      pre-shaper gain into Braids' moderate_overdrive.
//                         NOIS and TWNQ already end in that shaper at unity,
//                         so their detent value is 1.0 and the knob works in
//                         both directions; CLKN has no shaper in Braids, so
//                         its detent is 0 and the lower half is inert by
//                         construction, as z-filter's Bend is. Plaits'
//                         filtered-noise has no saturation stage of any kind,
//                         which is why the spare macro was spent here.
//
// RATE (SPEC R5). The three functions do not agree, which is why the engine
// runs a 2x internal loop and lets each model pick its own update rate:
//   - RenderFilteredNoise's loop is `while (size--)` (:1856) with no
//     `size -= 2`: a native 96 kHz algorithm. It runs every sub-sample.
//   - RenderTwinPeaksNoise ends in `size -= 2` (:1937) and writes each sample
//     twice: a 48 kHz algorithm zero-order-held up to 96 kHz. It updates on
//     the even sub-sample and holds through the odd one, which reproduces the
//     hold, not merely the rate.
//   - RenderClockedNoise's loop is `while (size--)` (:1987) with no
//     `size -= 2`: 96 kHz, and its clock is snapped to an integer number of
//     samples (`phase = phase_increment`, :2008), so the internal rate is
//     audible in the clock frequency itself. It runs every sub-sample.
// No model has internal oversampling on top of that. Because the port's
// internal rate is Braids' 96 kHz exactly, every rate constant transfers
// verbatim and nothing was re-derived.
//
// OUT / AUX. NOIS: the LP/BP/HP morph against the complementary notch, which
// the SVF already computes. TWNQ: y1 + y2 against y1 - y2, the mid/side pair
// of the two resonators. CLKN: two independent LCGs clocked and quantized
// together, so the two sides are genuinely decorrelated.
//
// MEASURED against the module -- eighteen cases in tests/ab.json, each model at
// both ends of both Braids axes. TWNQ lands at 0.00 dB of AC RMS and 0.00 to
// 0.03 dB across octave bands: it is the integer algorithm, so it agrees
// exactly. NOIS is within 0.07 dB of AC RMS and 0.11 dB of spectrum except in
// full high-pass, where it reads -0.31 dB and 0.34 dB with 57% of its energy in
// the top two bands -- that is the decimator, not the filter. CLKN is within
// 0.06 dB and 0.13 dB everywhere but a deliberately short loop, covered below.
// The port and the reference draw the SAME noise: both LCGs start at stmlib's
// 0x21 and consume one word per 96 kHz sample, so the two renders
// cross-correlate at 0.997 and these figures are systematic, not sampling
// noise.
//
// Declared deviations from Braids:
//   - the decimator is a 19-tap Hamming halfband. Braids has none, and the A/B
//     harness decimates the reference with a 127-tap Blackman-Harris one
//     (braids/test/render_braids_model.cc, Decimate2x); measured against that,
//     this one is within 0.13 dB to 16 kHz and reads -1.41 dB at 20 kHz and
//     -3.41 dB at 22.4 kHz. That top octave is where NOIS's high-pass case
//     spends its 0.34 dB.
//   - NOIS's SVF coefficients are closed forms on Plaits' corrected sample
//     rate rather than Braids' Q15 tables. They reproduce lut_svf_damp and
//     lut_svf_scale to 3.05e-05 absolute -- one LSB -- and lut_svf_cutoff to
//     2.03% relative worst case over MIDI 12..120 (at MIDI 17), 0.65% at MIDI
//     45 and 0.27-0.40% from MIDI 60 up; of that, 4.61 cents is the corrected rate
//     itself and the rest is the table's own Q15 truncation. It is worth
//     knowing what a 0.65% cutoff offset costs at high resonance. With the
//     damping at 0.0178 the SVF rings for 0.16 s, so a 2-second render holds
//     about twelve independent excitations of it and the offset changes WHICH
//     ones land: simulated on the identical noise sequence the AC RMS delta is
//     +1.07 dB over 2 s, +0.37 over 4 s, -0.06 over 8 s and -0.12 over 16 s,
//     against +0.015 / +0.001 / -0.000 with Braids' exact table value. The
//     A/B's resonant case renders eight seconds for that reason.
//   - lut_resonator_coefficient IS embedded, and deliberately NOT corrected
//     onto Plaits' sample rate. The table stores 2*cos(w) in Q15 and the
//     recursion consumes it as a Q15 integer, so one LSB is 116 cents of pole
//     at MIDI 45 and 249 cents at MIDI 36 -- the grid is far coarser than the
//     4.61-cent correction, and applying the correction moved the pole a full
//     step instead, taking the spectral delta from 0.27 dB to 0.60 dB at MIDI
//     45 and to 1.55 dB at MIDI 36. TWNQ therefore rings 4.61 cents under the
//     rest of the module's engines, which is nothing beside the +25.9 to +1641
//     cents the table already puts it out by.
//   - NOIS and the two shaper stages run in float; Braids' per-sample int16
//     requantization is not reproduced there, and against NOIS's full-scale
//     excitation it sits 84 dB down and does not show. TWNQ runs in int32
//     exactly as Braids does, because there it DOES show: floated, the model
//     read +1.15 dB of AC RMS at MIDI 36 with its skirts 1.5 dB low over an
//     eight-second render, the recursion's own truncation noise being the same
//     order as a three-level excitation. Its silence below MIDI 32 is
//     reproduced as well, and verified against the module at MIDI 24, where
//     both sides render exact zeros.
//   - CLKN's clock lands on a different divisor of the sample rate than the
//     module's at some notes. `phase = phase_increment` (:2008) forces the
//     clock onto an exact divisor, so at MIDI 45 it can only be 96000/109 =
//     880.7 Hz or 96000/110 = 872.7 Hz, one step being 15.8 cents; Braids
//     lands on 110 and the port, 0.27% higher off the corrected rate, on 109.
//     Measured +12.7 cents. It is not correctable -- the grid is coarser than
//     the correction, as with the resonator table -- and by MIDI 60 the
//     module's own step is already 38 cents.
//   - CLKN's parameter hysteresis (:1952-1959), a knob-jitter guard on
//     hardware, is dropped; the macros are already smooth.
//   - CLKN's looping sequence is seeded at Reset rather than from a word drawn
//     at Strike, so a short loop holds a different handful of random values
//     than the module's. At the shortest loops that is worth about 1 dB of AC
//     RMS, which is the statistics of seven numbers and not a level error.

#ifndef PLAITS_DSP_ENGINE2_NOISE_BANK_ENGINE_H_
#define PLAITS_DSP_ENGINE2_NOISE_BANK_ENGINE_H_

#include "stmlib/dsp/hysteresis_quantizer.h"

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids clamps pitch_ to [0, kHighestNote] (digital_oscillator.cc:118-122,
// kHighestNote = 140 * 128, i.e. MIDI 140 -- NOT kPitchTableStart, which is the
// 128 * 128 constant one line above it). TWNQ narrows that to MIDI 127.99 with
// its own CONSTRAIN (:1892, :1897), and NOIS's cutoff has been clamped to
// 12 kHz since MIDI 122.6, so the top twelve semitones only reach CLKN's clock.
const float kNoiseBankMaxNote = 140.0f;

// lut_svf_damp is `2 * (1 - resonance ** 0.25)` under a `minimum(2, 2/f -
// f * 0.5)` guard which -- checked over all 257 entries -- never binds, since
// f tops out at 0.7654 and the guard only bites above 0.828. Its index is
// parameter_[0] >> 7 with the remainder as the fraction, i.e. the 15-bit knob
// count divided by 128.
const float kNoiseBankDampIndexScale = 1.0f / 128.0f;
const float kNoiseBankResonanceDivisor = 260.0f;

// lut_svf_cutoff is `2 * sin(pi * f)` with f = cutoff / 96000 clamped to 1/8,
// i.e. a 12 kHz ceiling. Expressed here against the internal rate, which is
// 2x the output rate -- so the ceiling transfers unchanged.
const float kNoiseBankMaxCutoff = 0.125f;

// CLKN's clock: Braids shifts the note's phase increment left three times,
// stopping early rather than passing half the sample rate.
const int kNoiseBankClockShifts = 3;

// MACRO's range around the shaper. See the control note above for why the two
// filtered models and the clocked one take different stock values.
const float kNoiseBankMinDrive = 0.25f;
const float kNoiseBankMaxDrive = 4.0f;

enum NoiseBankModel {
  NOISE_BANK_MODEL_FILTERED,
  NOISE_BANK_MODEL_TWIN_PEAKS,
  NOISE_BANK_MODEL_CLOCKED,
  NOISE_BANK_MODEL_LAST
};

// The 19-tap halfband needs 19 sub-samples of history; the ring is rounded up
// to a power of two so the index is a mask.
const size_t kNoiseBankDecimatorSize = 32;
const uint32_t kNoiseBankDecimatorMask = 31;

class NoiseBankEngine : public Engine {
 public:
  NoiseBankEngine() { }
  ~NoiseBankEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_NOISE_BANK; }

 private:
  inline void Push(float main, float side) {
    decimator_main_[decimator_write_ & kNoiseBankDecimatorMask] = main;
    decimator_aux_[decimator_write_ & kNoiseBankDecimatorMask] = side;
    ++decimator_write_;
  }

  // NOIS: Chamberlin state variable filter.
  float lp_;
  float bp_;

  // TWNQ: two 2-pole resonators, plus the zero-order hold that carries their
  // 48 kHz update through the odd internal sub-sample. The recursion is int32
  // because its truncation noise is a real part of the model at low notes.
  int32_t y11_;
  int32_t y12_;
  int32_t y21_;
  int32_t y22_;
  float twin_main_;
  float twin_aux_;

  // CLKN: the clock, the loop that reseeds the sequence, and two independent
  // LCGs sharing both. Both phases stay uint32 -- Braids' snap onto an exact
  // divisor of the sample rate depends on the wraparound, and at the long loop
  // lengths a float accumulator would stall against its own epsilon.
  uint32_t clock_phase_;
  uint32_t cycle_phase_;
  uint32_t rng_state_;
  uint32_t rng_state_aux_;
  uint32_t seed_;
  uint32_t seed_aux_;
  int32_t clocked_sample_;
  int32_t clocked_sample_aux_;

  float drive_;

  float decimator_main_[kNoiseBankDecimatorSize];
  float decimator_aux_[kNoiseBankDecimatorSize];
  uint32_t decimator_write_;

  stmlib::HysteresisQuantizer2 model_quantizer_;

  DISALLOW_COPY_AND_ASSIGN(NoiseBankEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_NOISE_BANK_ENGINE_H_
