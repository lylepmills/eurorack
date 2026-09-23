// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// Listening build: alternatives for MORPH on three Braids ports whose MORPH
// was invented in the port and puts the module's own sound on a needle at
// noon. Each bank is one engine; each row is one way MORPH can behave.
//
//   AMBER  Plucked       row 0  CONTINUOUS  shipped: k * spread semitones
//                        row 1  STACKS      fifths/fourths, 5 positions
//                        row 2  CHORDS      9 shapes, unison at noon
//   GREEN  Struck Drum   row 0  linear      shipped
//                        row 1  cubic       same ends, wide hold at noon
//   RED    Cymbal        row 0  linear      shipped
//                        row 1  cubic       same ends, wide hold at noon
//
// FREQUENCY stays on pitch: MORPH is the thing being judged, so TWIST is not
// assigned to the knob. All three want TRIG patched -- unpatched they strike
// once and fall silent.
//
// Build with:
//   make -f plaits/makefile ENGINE_CONFIG=alt_firmwares/research/invented_controls/morph_ab_config.h

#ifndef PLAITS_DSP_ENGINE_CONFIG_H_
#define PLAITS_DSP_ENGINE_CONFIG_H_

#define PLAITS_HAS_TERRAIN_BANK 0
#define PLAITS_WAVE_TERRAIN_FACTORY_MASK 0xff
#define PLAITS_HAS_WAVETABLE_BANK 0
#define PLAITS_WAVETABLE_FACTORY_MASK 0x07
#define PLAITS_HAS_CUSTOM_WAVE_LINES 0
#ifndef PLAITS_BUILD_ENABLE_SYNC_INPUT
#define PLAITS_BUILD_ENABLE_SYNC_INPUT 0
#endif

#include "plaits/dsp/engine2/plucked_engine.h"
#include "plaits/dsp/engine2/struck_drum_engine.h"
#include "plaits/dsp/engine2/cymbal_engine.h"

#define PLAITS_ENGINE_COUNT 7
#define PLAITS_BANK_SIZES { 3, 2, 2 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 0, 1, 0, 1 }

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_CUSTOM_MODEL_DATA 0

#define PLAITS_ENGINE_MEMBERS \
  PluckedEngine plucked_continuous_; \
  PluckedEngine plucked_stacks_; \
  PluckedEngine plucked_chords_; \
  StruckDrumEngine struck_drum_linear_; \
  StruckDrumEngine struck_drum_cubic_; \
  CymbalEngine cymbal_linear_; \
  CymbalEngine cymbal_cubic_;

// Registration flags from the catalog: Struck Drum envelopes itself; Plucked
// and Cymbal go through the LPG.
#define PLAITS_REGISTER_ENGINES(registry) do { \
  plucked_continuous_.set_morph_mode(PLUCKED_MORPH_CONTINUOUS); \
  plucked_stacks_.set_morph_mode(PLUCKED_MORPH_STACKS); \
  plucked_chords_.set_morph_mode(PLUCKED_MORPH_CHORDS); \
  struck_drum_cubic_.set_morph_cubic(true); \
  cymbal_cubic_.set_morph_cubic(true); \
  (registry).RegisterInstance(&plucked_continuous_, false, 1.0f, 1.0f); \
  (registry).RegisterInstance(&plucked_stacks_, false, 1.0f, 1.0f); \
  (registry).RegisterInstance(&plucked_chords_, false, 1.0f, 1.0f); \
  (registry).RegisterInstance(&struck_drum_linear_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&struck_drum_cubic_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&cymbal_linear_, false, 1.0f, 1.0f); \
  (registry).RegisterInstance(&cymbal_cubic_, false, 1.0f, 1.0f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
