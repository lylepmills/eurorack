// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' SQUARE_SYNC and SAW_SYNC: a master oscillator at the played pitch
// hard-syncing a slave that sits a swept interval above it, with a crossfade
// between the two.
//
// The algorithm is Emilie Gillet's MacroOscillator::RenderDualSync driving two
// AnalogOscillator instances through RenderSquare / RenderSaw. The two Braids
// models differ ONLY in the shape handed to both oscillators
// (macro_oscillator.cc:261-262), so one engine covers both and MORPH selects
// between them.
//
// WHICH BRAIDS PARAMETER DOES WHAT, with lines (braids/macro_oscillator.cc):
//
//   :261-262  base_shape = SQUARE_SYNC ? OSC_SHAPE_SQUARE : OSC_SHAPE_SAW.
//             Not a parameter -- the model identity. Both oscillators get it.
//   :263,267  set_parameter(0) on BOTH. For a square that is
//             pw = (32768 - 0) << 16, i.e. exactly 0.5; for a saw the
//             parameter is unused. So neither Braids model has a pulse-width
//             control at all.
//   :265      master pitch = pitch_, the played note.
//   :269      slave pitch = pitch_ + (parameter_[0] >> 2).
//             parameter_[0] is TIMBRE. 0..32767 >> 2 = 0..8191, and Braids'
//             pitch unit is 1/128 semitone, so TIMBRE sweeps the sync interval
//             from 0 to 8191/128 = 63.99 semitones above the master.
//   :271      master Render() fills `buffer` AND emits sync_out.
//   :272      slave Render() takes that sync signal, fills temp_buffer_.
//   :274-279  BEGIN_INTERPOLATE_PARAMETER_1 / balance = parameter_1 << 1.
//             The suffix is the parameter INDEX: this interpolates
//             previous_parameter_[1] -> parameter_[1], which is COLOR, NOT
//             TIMBRE. Reading it the other way is the mistake that shipped in
//             fold's first version; here it would have put the interval sweep
//             and the master/slave balance on one knob and left COLOR dead.
//   :281      *buffer = (Mix(master, slave, balance) >> 2) * 3.
//             stmlib Mix(a, b, bal) = a + (b - a) * bal/65536, so COLOR 0 is
//             the master alone and COLOR 1 the slave alone; the >> 2) * 3 is a
//             flat 0.75 gain, which this port reproduces as kDualSyncOutputGain.
//
//   HARMONICS  Balance    Braids' COLOR: master through to synced slave.
//   TIMBRE     Interval   Braids' TIMBRE: the slave's pitch above the master,
//                         0 to 63.99 semitones.
//   MORPH      Shape      square (0.0) through to saw (1.0). At 0.0 this is
//                         SQUARE_SYNC and at 1.0 SAW_SYNC; the module has no
//                         path between them, since they are two menu entries.
//   MACRO      Reset      the phase the slave restarts at on each sync pulse.
//                         Braids always restarts it at zero (analog_oscillator
//                         .cc:262-264 and :325-327 both set phase_ =
//                         reset_time * increment); noon here is that exactly.
//
// LINEAGE. Plaits' virtual analog engine, VA_VARIANT 2 (virtual_analog_engine
// .cc:210-309), has a sync square: :250-252 derives square_sync_ratio from
// TIMBRE and :271-272 renders it. The differences are not cosmetic. There the
// ratio is (timbre - 0.5)^2 * 4 * 48, so it is flat zero across the whole lower
// half of the knob and reaches only 48 semitones at the top; the same knob also
// sets the square's pulse width (:247-248) and its gain (:254); the saw partner
// (:273) is NOT synced; and nothing crossfades master against slave -- MACRO
// there balances square against variable-saw.
// Dual Sync gives the interval its own knob over the full 64 semitones,
// holds the pulse width at the module's fixed 0.5, syncs the saw as well as
// the square, and puts the master/slave balance on a knob. The swept-interval
// sync-with-balance gesture is not reachable in the neighbour.
//
// RATE (SPEC R5). RenderSquare (analog_oscillator.cc:188-272) and RenderSaw
// (:274-335) each write ONE output sample per loop iteration from ONE
// `phase_ += phase_increment` (:227 and :306) and do NOT end in `size -= 2`.
// Neither has an internal oversampling step. They are therefore 96 kHz
// algorithms running at Braids' native rate, with no internal oversampling on
// top -- unlike the folders behind FOLD, which run 2x on top of 96 kHz.
//
// The phase-increment relation is rate-agnostic, so the pitch mapping needs no
// re-derivation. The BLEP residual is NOT rate-agnostic: at 96 kHz the residue
// of a 2-sample polyBLEP lands mostly above 24 kHz, where the reference
// renderer's decimator removes it, and at 48 kHz the same residue would fold
// straight back into the audible band. So this port runs 2x from 48 kHz to
// reach the same 96 kHz internal rate the module works at, and decimates.
//
// ANTI-ALIASING, measured, not asserted (SPEC R7). polyBLEP (stmlib
// ThisBlepSample / NextBlepSample, the same 0.5*t^2 pair Braids builds by hand
// at analog_oscillator.h:124-136) corrects five discontinuities: the master's
// wrap and its pulse edge, the slave's wrap and its pulse edge, and -- the one
// that carries this model -- the slave's sync reset, whose step is the naive
// value the slave had reached when the pulse arrived, taken to wherever Reset
// puts the restart.
//
// The residual is stated as non-harmonic energy against total energy, measured
// on 3 s renders with a 16384-point Blackman-Harris FFT (-92 dB sidelobes, so
// window leakage cannot be mistaken for aliasing) excluding +/-6 bins around
// every harmonic of the played pitch:
//
//                                          port 2x   Braids ref   port at 1x
//   note 45, Interval 0.5, Balance 0.5, sq  -46.4 dB   -46.7 dB     -38.1 dB
//   note 45, Interval 1.0, Balance 1.0, sq  -47.0 dB   -47.1 dB     -31.3 dB
//   note 45, Interval 1.0, Balance 1.0, saw -46.7 dB   -46.9 dB     -25.6 dB
//
// So the port sits within 0.3 dB of the module everywhere measured, which is
// the point of matching its internal rate rather than picking a rate. The
// third column is this same engine built with kDualSyncOversampling = 1 and
// the decimator bypassed: running the sync at the output rate costs 8.4 dB at
// the stock centre and 21.1 dB at the corner where the slave is highest and
// the balance is entirely on it. That is the measurement the 2x stage is
// bought with.
//
// The decimator is a 95-tap Kaiser (beta 12) halfband -- 24 multiplies plus
// the centre tap per output sample, since a halfband's even taps are zero.
// Response, computed from the embedded coefficients: -0.00 dB at 18 kHz,
// +0.00 dB at 20 kHz, -0.30 dB at 22 kHz, -1.81 dB at 23 kHz, -6.02 dB at
// 24 kHz, -14.52 dB at 25 kHz, -29.30 dB at 26 kHz, -118 dB at 28 kHz.
//
// Accepted residue: the reference renderer decimates with a 127-tap
// Blackman-Harris sinc, which reads -1.06 / -18.76 / -45.10 dB at 23 / 25 /
// 26 kHz, so between 24 and 26 kHz this port folds back more than the
// reference does. It lands entirely in the 20.5-24 kHz band, and the A/B
// measures the cost: across the fourteen cases that band runs +1.74 dB at the
// worst (note 72, slave alone), +0.96 dB at the next (note 72 with the master
// still in the mix) and within -0.53..+0.19 dB everywhere else, against
// 2.0-7.9% of the energy. A 63-tap halfband was tried first and put the same
// band at +4.57 dB; going past 95 taps buys progressively less for a
// difference confined to the top band.
//
// OUT: the master/slave crossfade at Braids' balance. AUX (mono): the
// complementary crossfade, so it always favours the side OUT does not; the two
// coincide at Balance 0.5, as fold's do at its blend centre. In stereo OUT/AUX
// become L/R and take small opposing balance offsets.
//
// Declared deviations from Braids:
//   - the crossfade is applied AFTER decimation rather than at 96 kHz. Both
//     operations are linear, so this is exact for a fixed Balance and differs
//     only by the coefficient's motion across the filter's 95-sample span,
//     which is 0.99 ms at the internal rate.
//   - Braids quantises the sync pulse's position to 1/128 of a sample
//     (analog_oscillator.cc:234) before the slave reads it back; the port
//     passes the exact fractional reset time.
//   - Braids quantises the sync interval to 1/128 semitone (the >> 2 above);
//     the port sweeps it continuously, so a given TIMBRE setting can put the
//     slave up to 0.39 cents away from where the module puts it.
//   - the slave's frequency is clamped to kMaxFrequency expressed at the
//     output rate (SPEC R4), which is 11.97 kHz. Braids clamps its pitch at
//     MIDI 128 instead (analog_oscillator.cc:47-49), i.e. 13.29 kHz, so the
//     port stops the interval sweep 1.81 semitones lower. The two agree until
//     note + Interval passes MIDI 126.2.
//   - no DC blocker, matching the module: a hard-synced slave carries DC that
//     moves with the interval, and Braids emits it. Measured across the five
//     scenarios, |DC| stays under 0.028.
//
// LEVEL (SPEC R1). Braids' own output is pinned at 0.75 of full scale by the
// int16 (Mix >> 2) * 3, and this port applies the same 0.75 -- but its peak is
// NOT pinned there, because polyBLEP overshoot and the decimator's ringing
// both sit on top of the naive waveform. The peak is a NARROW function of
// Interval, so a coarse grid understates it badly and the sweep has to be fine:
// stepping Interval at 0.005 the engine-domain peak reaches 1.015 with MACRO at
// NOON -- already past full scale on the plane Braids itself can reach (note 72,
// Balance 1.0, Interval 0.73, square) -- and 1.237 once Reset moves off noon
// (note 84, Interval 0.57, square, Reset 1.0). The package's own
// `plaits_lab check --full` shows the same thing from the other side: the `high`
// scenario, which pins MACRO at noon, renders at 0.8889 with |out_gain| 0.9
// applied, i.e. 0.988 in the engine domain. So the peak cannot be pinned
// analytically at any level worth shipping, and the gains are NEGATIVE: voice.h
// routes the render through stmlib::Limiter (voice.h:87-89) rather than straight
// into Clip16. The SDK's host renderer applies |out_gain| only, so this choice
// does not move any A/B number.

#ifndef PLAITS_DSP_ENGINE2_DUAL_SYNC_ENGINE_H_
#define PLAITS_DSP_ENGINE2_DUAL_SYNC_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// (parameter_[0] >> 2) at TIMBRE 1 is 8191, and Braids' pitch unit is 1/128
// semitone (macro_oscillator.cc:269).
const float kDualSyncMaxInterval = 8191.0f / 128.0f;

// set_parameter(0) on a square is pw = (32768 - 0) << 16 = half of the phase
// range (analog_oscillator.cc:206). Neither model exposes pulse width.
const float kDualSyncPulseWidth = 0.5f;

// (Mix(...) >> 2) * 3, macro_oscillator.cc:281.
const float kDualSyncOutputGain = 0.75f;

// Reset travels half a cycle either side of Braids' zero, which is as far as
// it can go before wrapping back toward the module's own behaviour.
const float kDualSyncMaxResetPhase = 0.5f;

// The two channels take opposing balance offsets in stereo. Small, because the
// crossfade is the model's whole gesture; a wide split would make the two
// sides master-alone and slave-alone rather than a stereo image of one sound.
const float kDualSyncStereoBalance = 0.12f;

// 2x, to reach Braids' 96 kHz internal rate from 48 kHz (see RATE above).
const int kDualSyncOversampling = 2;

// The halfband decimator: 95 taps, held in a power-of-two ring so the window
// index masks instead of branching. Only (95 + 1) / 4 = 24 symmetric pairs and
// the centre tap are non-zero.
const int kDualSyncHalfbandLength = 95;
const int kDualSyncHalfbandPairs = 24;
const int kDualSyncDecimatorSize = 128;

class DualSyncEngine : public Engine {
 public:
  DualSyncEngine() { }
  ~DualSyncEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_DUAL_SYNC; }

 private:
  float master_phase_;
  float slave_phase_;
  bool master_high_;
  bool slave_high_;
  float master_next_sample_;
  float slave_next_sample_;

  float master_frequency_;
  float slave_frequency_;
  float balance_;
  float shape_;
  float reset_phase_;

  float master_history_[kDualSyncDecimatorSize];
  float slave_history_[kDualSyncDecimatorSize];
  int decimator_write_;

  DISALLOW_COPY_AND_ASSIGN(DualSyncEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_DUAL_SYNC_ENGINE_H_
