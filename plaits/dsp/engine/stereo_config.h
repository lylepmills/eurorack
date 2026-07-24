// Copyright 2026 Rubato Audio.
//
// Per-engine stereo build gate.
//
// The stereo aux-output render path (OUT/AUX as a true L/R pair) costs flash
// per engine, and only the engines a build actually enables need it. Each
// stereo-capable engine guards its stereo render branch with
// `if (PLAITS_STEREO_<X> && parameters.stereo)` and reports
// `stereo_capable() { return PLAITS_STEREO_<X>; }`. When the flag is 0 the
// branch's condition is a compile-time false, so the branch (and the code only
// it reaches) is dead-stripped, and the voice never routes stereo to that
// engine.
//
// Every flag defaults to 1 here, so host tests, local firmware, and stock
// builds carry stereo for every engine unchanged. The hosted builder passes
// -DPLAITS_STEREO_<X>=0 per object for the engines a recipe did NOT enable, so
// each engine object caches as just two variants (on / off) and the build stays
// warm. The macro name is the engine's catalog id, upper-cased with '-' -> '_'
// (the three DX7 banks share the six-op engine, hence PLAITS_STEREO_SIX_OP).

#ifndef PLAITS_DSP_ENGINE_STEREO_CONFIG_H_
#define PLAITS_DSP_ENGINE_STEREO_CONFIG_H_

#ifndef PLAITS_STEREO_VIRTUAL_ANALOG
#define PLAITS_STEREO_VIRTUAL_ANALOG 1
#endif
#ifndef PLAITS_STEREO_WAVESHAPING
#define PLAITS_STEREO_WAVESHAPING 1
#endif
#ifndef PLAITS_STEREO_TWO_OP_FM
#define PLAITS_STEREO_TWO_OP_FM 1
#endif
#ifndef PLAITS_STEREO_GRANULAR_FORMANT
#define PLAITS_STEREO_GRANULAR_FORMANT 1
#endif
#ifndef PLAITS_STEREO_HARMONIC
#define PLAITS_STEREO_HARMONIC 1
#endif
#ifndef PLAITS_STEREO_WAVETABLE
#define PLAITS_STEREO_WAVETABLE 1
#endif
#ifndef PLAITS_STEREO_CHORDS
#define PLAITS_STEREO_CHORDS 1
#endif
#ifndef PLAITS_STEREO_SPEECH
#define PLAITS_STEREO_SPEECH 1
#endif
#ifndef PLAITS_STEREO_SWARM
#define PLAITS_STEREO_SWARM 1
#endif
#ifndef PLAITS_STEREO_FILTERED_NOISE
#define PLAITS_STEREO_FILTERED_NOISE 1
#endif
#ifndef PLAITS_STEREO_PARTICLE_NOISE
#define PLAITS_STEREO_PARTICLE_NOISE 1
#endif
#ifndef PLAITS_STEREO_INHARMONIC_STRING
#define PLAITS_STEREO_INHARMONIC_STRING 1
#endif
#ifndef PLAITS_STEREO_MODAL_RESONATOR
#define PLAITS_STEREO_MODAL_RESONATOR 1
#endif
#ifndef PLAITS_STEREO_ANALOG_BASS_DRUM
#define PLAITS_STEREO_ANALOG_BASS_DRUM 1
#endif
#ifndef PLAITS_STEREO_ANALOG_SNARE
#define PLAITS_STEREO_ANALOG_SNARE 1
#endif
#ifndef PLAITS_STEREO_ANALOG_HI_HAT
#define PLAITS_STEREO_ANALOG_HI_HAT 1
#endif
#ifndef PLAITS_STEREO_VIRTUAL_ANALOG_VCF
#define PLAITS_STEREO_VIRTUAL_ANALOG_VCF 1
#endif
#ifndef PLAITS_STEREO_WAVE_TERRAIN
#define PLAITS_STEREO_WAVE_TERRAIN 1
#endif
#ifndef PLAITS_STEREO_CHIPTUNE
#define PLAITS_STEREO_CHIPTUNE 1
#endif
#ifndef PLAITS_STEREO_SIX_OP
#define PLAITS_STEREO_SIX_OP 1
#endif
#ifndef PLAITS_STEREO_GLISSON
#define PLAITS_STEREO_GLISSON 1
#endif
#ifndef PLAITS_STEREO_GENDY
#define PLAITS_STEREO_GENDY 1
#endif
#ifndef PLAITS_STEREO_SCANNED
#define PLAITS_STEREO_SCANNED 1
#endif
#ifndef PLAITS_STEREO_LOOPBACK
#define PLAITS_STEREO_LOOPBACK 1
#endif
#ifndef PLAITS_STEREO_LOCKSTEP
#define PLAITS_STEREO_LOCKSTEP 1
#endif
#ifndef PLAITS_STEREO_TAPFIELD
#define PLAITS_STEREO_TAPFIELD 1
#endif
#ifndef PLAITS_STEREO_PHASE_WEAVE
#define PLAITS_STEREO_PHASE_WEAVE 1
#endif
#ifndef PLAITS_STEREO_SIDEBAND_BANK
#define PLAITS_STEREO_SIDEBAND_BANK 1
#endif
#ifndef PLAITS_STEREO_UNDERTOW
#define PLAITS_STEREO_UNDERTOW 1
#endif
#ifndef PLAITS_STEREO_REED_PIPE
#define PLAITS_STEREO_REED_PIPE 1
#endif
#ifndef PLAITS_STEREO_PHASE_FLOCK
#define PLAITS_STEREO_PHASE_FLOCK 1
#endif
#ifndef PLAITS_STEREO_RULEFIELD
#define PLAITS_STEREO_RULEFIELD 1
#endif

#endif  // PLAITS_DSP_ENGINE_STEREO_CONFIG_H_
