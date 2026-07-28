// Copyright 1995-2023 Perry R. Cook and Gary P. Scavone.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// STK's BandedWG -- Essl and Cook's banded waveguides -- as one Plaits engine.
//
// A banded waveguide gives each mode of a stiff object its own delay loop
// closed by a bandpass at that mode's frequency. Because every mode is a
// travelling wave rather than a filter, an exciter placed in the loop
// interacts with all of them at once -- which is the point: a bow needs
// something to grab and release against, and a bank of resonators has no
// travelling wave to grab.
//
// WHAT THE CATALOG DOES NOT ALREADY HAVE. `modal-resonator` strikes a bank of
// modes; `bowed` bows a string. Nothing bows a BAR. That is the entire gap
// this fills, and it is a wide one: bowed vibraphone, musical saw, glass
// harmonica, and the Tibetan prayer bowl that Essl and Cook presented at
// ICMC'02 are all one preset knob apart, and none of them is reachable from a
// struck modal bank or a bowed string.
//
// FOUR MATERIALS on HARMONICS, from STK's presets: uniform bar (modes at 1,
// 2.756, 5.404, 8.933 -- the free-free bar's inharmonic series), tuned bar
// (a marimba bar, undercut so the second mode lands two octaves up), glass
// harmonica, and the prayer bowl. The bowl is the interesting one and the
// reason the mode count goes to six: its modes come in near-unison PAIRS
// (0.9961/1.0039, 2.979/2.993, 5.704/5.704) and the slow beating between each
// pair is the sound. Truncating it to four modes would keep two pairs; upstream
// ships twelve modes, of which the top six are a long tail of high partials
// that cost two more delay lines each and are the first thing to go.
//
// THE PATENT NOTE UPSTREAM CARRIES IS SPENT. STK's header warns that waveguide
// models may be "subject to patents held by Stanford University, Yamaha, and
// others". The Stanford waveguide patent is US 4,984,276, filed 1989-09-27 and
// issued 1991-01-08; under the pre-1995 term rule -- the later of 17 years from
// issue and 20 years from filing -- it expired no later than 2009-09-27.
//
// MEMORY IS THE REAL CONSTRAINT, not flash. Each mode needs a delay line of
// fs/(f0 * ratio) samples, so the bill is set by the LOWEST note times the
// SMALLEST mode ratio in any preset. Sized here for A1 (55 Hz) against the
// bowl's 0.9961 and 1.0039 -- two nearly-full-length lines -- which comes to
// about 10.5 KB of the voice's 16,384-byte arena. Notes below A1 fold up by
// octaves rather than allocating for a register no bar or bowl occupies.

#ifndef PLAITS_DSP_ENGINE2_BANDED_WAVEGUIDE_ENGINE_H_
#define PLAITS_DSP_ENGINE2_BANDED_WAVEGUIDE_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kBandedWgNumPresets = 4;
const int kBandedWgMaxModes = 6;

// The lowest note that gets its own delay lines; below this the engine folds
// up by octaves.
const float kBandedWgLowestNote = 33.0f;  // A1, 55 Hz.

// STK clamps the fundamental here because a delay shorter than two samples is
// not a delay.
const float kBandedWgHighestFrequency = 1568.0f;

// Per-slot delay capacity, sized for kBandedWgLowestNote against the smallest
// mode ratio any preset uses in that slot. Slots 0 and 1 are both nearly
// full-length because of the prayer bowl's unison pair.
const int kBandedWgDelaySize0 = 880;
const int kBandedWgDelaySize1 = 876;
const int kBandedWgDelaySize2 = 296;
const int kBandedWgDelaySize3 = 294;
const int kBandedWgDelaySize4 = 156;
const int kBandedWgDelaySize5 = 156;

// STK's fixed bandpass pole radius, 1 - pi*32/fs.
const float kBandedWgBandwidth = 32.0f;

// The loop gain ceiling. The bandpasses are peak-normalised and the delays are
// lossless, so the loop gain IS this number: at 1.0 the engine oscillates on
// its own.
const float kBandedWgMaxLoopGain = 0.9999f;

struct BandedWgPreset {
  const char* name;
  int num_modes;
  const float* modes;
  const float* base_gains;
  const float* excitation;
};

extern const BandedWgPreset kBandedWgPresets[kBandedWgNumPresets];

class BandedWaveguideEngine : public Engine {
 public:
  BandedWaveguideEngine() { }
  ~BandedWaveguideEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  // Pattern A: the full bank against its fundamental band alone.
  virtual bool stereo_capable() const { return true; }

 private:
  void Pluck(float amplitude);

  // Plain ring buffers rather than stmlib::DelayLine: the capacities differ
  // per slot and are only known at Init, which a templated fixed-size line
  // cannot express.
  float* delay_line_[kBandedWgMaxModes];
  int delay_capacity_[kBandedWgMaxModes];
  int delay_length_[kBandedWgMaxModes];
  int delay_write_[kBandedWgMaxModes];

  // Peak-normalised two-pole bandpass per mode.
  float bp_a1_[kBandedWgMaxModes];
  float bp_a2_[kBandedWgMaxModes];
  float bp_b0_[kBandedWgMaxModes];
  float bp_x1_[kBandedWgMaxModes];
  float bp_x2_[kBandedWgMaxModes];
  float bp_y1_[kBandedWgMaxModes];
  float bp_y2_[kBandedWgMaxModes];

  float gain_normalization_;
  int num_modes_;
  int preset_;
  float previous_frequency_;
  float velocity_input_;

  DISALLOW_COPY_AND_ASSIGN(BandedWaveguideEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_BANDED_WAVEGUIDE_ENGINE_H_
