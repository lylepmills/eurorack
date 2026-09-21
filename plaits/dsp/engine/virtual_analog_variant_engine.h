// Copyright 2016 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Virtual Analog Variant -- Emilie Gillet's two alternate virtual-analog
// designs preserved in the Plaits source, unified into one engine.
//
// The two originals are duals of each other. Both have a primary and a
// detuned secondary oscillator and both spend HARMONICS on the interval
// between them; they differ only in what they buy with their third knob:
//
//   Dual       TIMBRE and MORPH are INDEPENDENT waveshapes, one per
//              oscillator, and the mix is hard-coded at 50/50.
//   Crossfade  TIMBRE sweeps a TRAJECTORY -- detuned pair, through the dry
//              primary, into hard sync -- and both oscillators share MORPH's
//              single shape.
//
// Neither can have both, because a three-knob engine has run out of knobs.
// TWIST is the missing fourth, so this engine carries both features at once
// and is a strict superset of the two:
//
//   HARMONICS  detune interval (quantized to unison / fifth / octave /
//              octave+fifth / two octaves by the same double smoothstep), and
//              the AUX hard-sync ratio
//   TIMBRE     primary waveshape
//   MORPH      the trajectory
//   TWIST      how far the secondary's shape departs from the primary's.
//              Matched at noon, diverging either side.
//
// WHICH KNOB CARRIES THE TRAJECTORY is the one judgement call here, and it is
// deliberately NOT the obvious one. TWIST is only reachable by giving up the
// FREQUENCY knob or a CV input, so most players never assign it and macro sits
// at exactly 0.5 forever (Voice::Render pins it there). Putting the trajectory
// on TWIST would therefore hide the engine's whole character behind a menu
// walk and freeze it at one point for everyone else. Putting the SHAPE SPREAD
// there hides only a refinement: at noon the two oscillators share a shape,
// which is exactly Crossfade, so the default sound is a complete engine rather
// than a fixed slice of one. PLAITS_VA_VARIANT_TRAJECTORY_ON_TWIST=1 builds
// the other arrangement for comparison.
//
// AUX keeps Dual's: the primary against a hard-synced secondary at 50/50, its
// ratio set by HARMONICS over 48 semitones -- independent of the trajectory,
// so it stays a usable second voice wherever MORPH sits. Crossfade's AUX (the
// bare detuned oscillator) is the one thing the union does not reproduce; it
// carries the same pitch material OUT is already blending, so it is the less
// distinct of the two.
//
// Stereo generalises Crossfade's rule: L and R are two nearby points either
// side of wherever the trajectory sits, mono sum unchanged. Dual's fixed
// 0.5 +/- width is that rule with the trajectory parked at the detuned end.
// AUX becomes the right channel there, so the sync voice steps aside exactly
// as it already does in both originals.

#ifndef PLAITS_DSP_ENGINE_VIRTUAL_ANALOG_VARIANT_ENGINE_H_
#define PLAITS_DSP_ENGINE_VIRTUAL_ANALOG_VARIANT_ENGINE_H_

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/oscillator/variable_shape_oscillator.h"

#ifndef PLAITS_VA_VARIANT_TRAJECTORY_ON_TWIST
#define PLAITS_VA_VARIANT_TRAJECTORY_ON_TWIST 0
#endif

#ifndef PLAITS_STEREO_VIRTUAL_ANALOG_VARIANT
#define PLAITS_STEREO_VIRTUAL_ANALOG_VARIANT 1
#endif

namespace plaits {

// Remedies for the trajectory's DEAD CENTRE, selectable per instance so one
// firmware can carry them side by side.
//
// The dead centre is inherited, not invented: at the trajectory's dry point
// there is no second oscillator, so every control that only shapes the second
// oscillator goes inert. Crossfade measures identically -- HARMONICS moves the
// output by 0.52480 RMS at the detuned end and EXACTLY 0.00000 at its
// midpoint. Dual is flat at 0.52480 everywhere, but only because its mix is
// hard-coded at 50/50 and so has no trajectory to have a dry point in. Giving
// the trajectory a knob is what buys the superset, and the dead centre is its
// price. Each remedy below pays for it somewhere else.
enum VariantRemedy {
  // Crossfade's trajectory verbatim. Two controls inert at the midpoint --
  // one worse than Crossfade, where MORPH still shapes the primary there.
  VARIANT_REMEDY_NONE = 0,
  // Drop the squaring on the lower half. Emilie squares the detuned fade and
  // leaves the sync side linear, so the lower half is mush: 12.5% of full
  // response at a quarter travel, 2.0% at 0.40. This straightens it. Narrows
  // the dead REGION; the dead POINT stays, and Crossfade's OUT stops being
  // bit-exact.
  VARIANT_REMEDY_LINEAR_FADE,
  // TWIST moves BOTH shapes, in opposite directions about TIMBRE, so it stays
  // audible at the dry point where only the primary is sounding. Restores
  // parity with Crossfade (one control live there, not zero). Costs
  // independent shape placement, so the Dual equivalence weakens.
  VARIANT_REMEDY_SHAPE_SPREAD,
  // Remove the dry point entirely: the primary stays at 50% throughout and the
  // trajectory crossfades the SECONDARY from detuned to hard-synced. Nothing
  // is ever inert. The endpoints become Dual's OUT and Dual's AUX. Costs
  // Crossfade's dry and full-sync ends outright, and both secondaries now
  // render every block, so it is the only remedy that also costs CPU.
  VARIANT_REMEDY_NO_DRY
};

class VirtualAnalogVariantEngine : public Engine {
 public:
  VirtualAnalogVariantEngine() : remedy_(VARIANT_REMEDY_NONE) { }
  ~VirtualAnalogVariantEngine() { }

  // Set at registration, which runs before Voice::Init calls Init() on each
  // engine; neither Init() nor Reset() touches it.
  void set_remedy(VariantRemedy remedy) { remedy_ = remedy; }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const {
    return PLAITS_STEREO_VIRTUAL_ANALOG_VARIANT;
  }
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  virtual bool hard_sync_capable() const { return true; }
#endif
  // Qualified, not inherited: every oscillator here runs through
  // RenderLinearFm with the signed offset scaled by its own ratio to the
  // primary, and extended_tzfm_test exercises the strict through-zero checks
  // (negative frequency must not be clamped, and FM must reverse phase rather
  // than rectify) that both parents are exempt from.
  virtual bool linear_tzfm_capable() const { return true; }
  virtual bool fast_fm_capable() const { return true; }

 private:
  float ComputeDetuning(float detune) const;

  VariableShapeOscillator primary_;
  VariableShapeOscillator auxiliary_;
  VariableShapeOscillator sync_;
  VariableShapeOscillator aux_sync_;

  float auxiliary_amount_;
  float xmod_amount_;
  float* temp_buffer_;
  // Only VARIANT_REMEDY_NO_DRY needs both secondaries at once; the others keep
  // the mutually-exclusive single-buffer path.
  float* secondary_buffer_;
  VariantRemedy remedy_;

  DISALLOW_COPY_AND_ASSIGN(VirtualAnalogVariantEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_VIRTUAL_ANALOG_VARIANT_ENGINE_H_
