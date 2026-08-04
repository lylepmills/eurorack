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

// Recipe builds can replace the five shipped LPC word banks with a selected
// stock subset followed by host-generated decoded-frame banks.
#ifndef PLAITS_HAS_CUSTOM_SPEECH_BANKS
#define PLAITS_HAS_CUSTOM_SPEECH_BANKS 0
#endif

#if PLAITS_ENGINE_COUNT < 1 || PLAITS_ENGINE_COUNT > 32
#error "PLAITS_ENGINE_COUNT must be between 1 and 32"
#endif

// Per-bank engine counts, in the module's internal display rotation (amber,
// green, red for three banks; orange, green, red, amber for four — see
// Ui::BankToColor). One to four banks; each holds 0..8
// engines. A bank with
// fewer than eight is a "short bank" whose select-button cycle wraps at its real
// size (see bank_navigation.h). This is a COUNT per bank — where a bank's gaps
// sit (a "sparse bank") is carried separately by PLAITS_ENGINE_ROWS below. The
// values must sum to PLAITS_ENGINE_COUNT and none may exceed eight — both checked
// with static_assert in ui.cc. The default is three full banks of eight (24),
// byte-identical to before.
#ifndef PLAITS_BANK_SIZES
#define PLAITS_BANK_SIZES { 8, 8, 8 }
#endif

// Physical LED row (0..7) of each engine within its bank, in the same registry
// order as the engine list (amber/green/red for three banks;
// orange/green/red/amber for four) — one
// entry per engine, summing to PLAITS_ENGINE_COUNT. This lets a bank keep gaps
// in place: an engine holds its own row instead of the bank compacting to the
// front, so deleting a mid-bank model leaves the others on their LEDs and their
// select-button positions. The generated header emits it alongside
// PLAITS_BANK_SIZES; the default is the identity mapping (each bank filled
// front-to-back 0..7), byte-identical to the pre-sparse-bank behavior.
#ifndef PLAITS_ENGINE_ROWS
#define PLAITS_ENGINE_ROWS \
    { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 }
#endif

#ifndef PLAITS_BUILD_NAVIGATION_MODE
#define PLAITS_BUILD_NAVIGATION_MODE 0
#endif

// Replace hue-dependent UI states with yellow brightness levels. Banks use
// 100%, 50%, 25%, and 12.5% in public palette order; the options menu uses
// 100%, 50%, and 25% within each blink tier. The firmware registry is ordered
// amber, green, red for three banks and orange, green, red, amber for four, so
// Ui::BankToColor maps those internal indices back to the public levels. This
// is baked into the firmware
// rather than stored in settings: the right-button power-up gesture remains
// available for the optional calibration procedure.
#ifndef PLAITS_BUILD_COLOR_BLIND_MODE
#define PLAITS_BUILD_COLOR_BLIND_MODE 0
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

#ifndef PLAITS_BUILD_AUX_OUTPUT_OPTION
#define PLAITS_BUILD_AUX_OUTPUT_OPTION 0
#endif

#ifndef PLAITS_BUILD_AUX_SUBOSC_OPTION
#define PLAITS_BUILD_AUX_SUBOSC_OPTION 0
#endif

#ifndef PLAITS_BUILD_CHORD_SET_OPTION
#define PLAITS_BUILD_CHORD_SET_OPTION 0
#endif

#ifndef PLAITS_CHORD_TABLE_COUNT
#define PLAITS_CHORD_TABLE_COUNT 3
#endif

// Scale bank shared by the Diatonic Chord and Scale Stack engines. Pitches use
// Braids' native 1/128-semitone units; each entry contains seven padded pitch
// slots followed by the actual degree count. Hosted builds replace this bank
// from the recipe. Ordinary builds retain the original eight shipped scales.
#ifndef PLAITS_SCALE_BANK_COUNT
#define PLAITS_SCALE_BANK_COUNT 8
#endif

#ifndef PLAITS_SCALE_BANK
#define PLAITS_SCALE_BANK \
  { \
    { { 0, 256, 512, 640, 896, 1152, 1408 }, 7 }, \
    { { 0, 256, 384, 640, 896, 1024, 1280 }, 7 }, \
    { { 0, 256, 384, 640, 896, 1152, 1280 }, 7 }, \
    { { 0, 256, 512, 640, 896, 1152, 1280 }, 7 }, \
    { { 0, 256, 384, 640, 896, 1024, 1408 }, 7 }, \
    { { 0, 256, 384, 640, 896, 1152, 1408 }, 7 }, \
    { { 0, 256, 512, 896, 1152, 0, 0 }, 5 }, \
    { { 0, 256, 512, 768, 1024, 1280, 0 }, 6 }, \
  }
#endif

#ifndef PLAITS_BUILD_HOLD_ON_TRIGGER_OPTION
#define PLAITS_BUILD_HOLD_ON_TRIGGER_OPTION 0
#endif

// Include the CV calibration procedure (off by default). The alt firmware has
// shipped without it since 2023: calibration DATA lives in its own flash chunk
// (settings.h, tag CALI, at 0x08004000) which the audio bootloader never erases
// — it only writes from 0x08008000 — so a module calibrated under any other
// Plaits firmware keeps tracking correctly here, and the procedure itself is
// dead weight for almost everyone. It matters for a module that has never been
// calibrated (a DIY build) or whose settings flash was erased over SWD, so a
// recipe can opt back in and pay the flash for it. Entered by holding the RIGHT
// button while powering the module up — the left button is the bootloader's
// firmware-update gesture, so the right one is the free half of that idiom.
#ifndef PLAITS_BUILD_ENABLE_CALIBRATION
#define PLAITS_BUILD_ENABLE_CALIBRATION 0
#endif

// Plum Audio Ro'Ved replaces Plaits' two model buttons with four clickable
// knobs. The DSP and analog hardware are Plaits-compatible; only the switch
// GPIOs and UI gestures differ. Hosted recipes select this panel variant with
// the `plum-audio-roved` target (schema v15).
#ifndef PLAITS_ROVED_PANEL
#define PLAITS_ROVED_PANEL 0
#endif

// The id a locally built (non-hosted) firmware stamps into saved settings. It
// must equal what the builder mints for an all-default recipe, or switching
// between a local build and a hosted default build resets options each way.
// Currently OPTIONS_LAYOUT_VERSION 4 with every option at value 0; recompute
// with generate_engine_config.validate_recipe(default_recipe.json) after any
// change to the fold.
#ifndef PLAITS_BUILD_OPTIONS_PROFILE_ID
#define PLAITS_BUILD_OPTIONS_PROFILE_ID 0x02debeu
#endif

#ifndef PLAITS_BUILD_ATTENUVERTER_MODE
#define PLAITS_BUILD_ATTENUVERTER_MODE 0
#endif

#endif  // PLAITS_BUILD_CONFIG_H_
