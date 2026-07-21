// Copyright 2026 Rubato Audio.
//
// Compile-time preferences for Plaits Lab builds. The hosted builder defines
// these values in its generated, force-included configuration header. Local
// and stock builds use the original alternate-firmware behavior below.

#ifndef PLAITS_BUILD_CONFIG_H_
#define PLAITS_BUILD_CONFIG_H_

// Number of engine slots. 24 (three banks of eight) is the default; a
// generated recipe may opt in to 32 (a fourth bank, shown orange on the
// LEDs). 32 is a hard ceiling: the speech/chiptune behavior masks are
// uint32_t bitfields indexed by slot.
#ifndef PLAITS_ENGINE_COUNT
#define PLAITS_ENGINE_COUNT 24
#endif

#if PLAITS_ENGINE_COUNT < 1 || PLAITS_ENGINE_COUNT > 32
#error "PLAITS_ENGINE_COUNT must be between 1 and 32"
#endif

// Per-bank engine counts, in the module's display order (green, red, amber, and
// optionally orange). One to four banks; each holds 0..8 engines. A bank with
// fewer than eight is a "short bank" whose select-button cycle wraps at its real
// size (see bank_navigation.h). The values must sum to PLAITS_ENGINE_COUNT and
// none may exceed eight — both checked with static_assert in ui.cc. The default
// is three full banks of eight (24), byte-identical to before.
#ifndef PLAITS_BANK_SIZES
#define PLAITS_BANK_SIZES { 8, 8, 8 }
#endif

#ifndef PLAITS_BUILD_NAVIGATION_MODE
#define PLAITS_BUILD_NAVIGATION_MODE 0
#endif

#ifndef PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION
#define PLAITS_BUILD_LOCKED_FREQUENCY_POT_OPTION 0
#endif

#ifndef PLAITS_BUILD_MODEL_CV_OPTION
#define PLAITS_BUILD_MODEL_CV_OPTION 0
#endif

#ifndef PLAITS_BUILD_LEVEL_CV_OPTION
#define PLAITS_BUILD_LEVEL_CV_OPTION 0
#endif

#ifndef PLAITS_BUILD_AUX_SUBOSC_WAVE_OPTION
#define PLAITS_BUILD_AUX_SUBOSC_WAVE_OPTION 0
#endif

#ifndef PLAITS_BUILD_AUX_SUBOSC_OCTAVE_OPTION
#define PLAITS_BUILD_AUX_SUBOSC_OCTAVE_OPTION 0
#endif

#ifndef PLAITS_BUILD_CHORD_SET_OPTION
#define PLAITS_BUILD_CHORD_SET_OPTION 0
#endif

#ifndef PLAITS_CHORD_TABLE_COUNT
#define PLAITS_CHORD_TABLE_COUNT 3
#endif

#ifndef PLAITS_BUILD_HOLD_ON_TRIGGER_OPTION
#define PLAITS_BUILD_HOLD_ON_TRIGGER_OPTION 0
#endif

#ifndef PLAITS_BUILD_OPTIONS_PROFILE_ID
#define PLAITS_BUILD_OPTIONS_PROFILE_ID 0x0002u
#endif

#endif  // PLAITS_BUILD_CONFIG_H_
