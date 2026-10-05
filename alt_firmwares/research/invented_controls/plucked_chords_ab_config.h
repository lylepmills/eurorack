// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
// Listening build: Plucked with MORPH choosing chords, unison at fully CCW.
// One bank, seven rows; every row sequences a chord's tones strike by strike
// from its first tone, restarting when the chord changes.
//   row 0  CHORDS_CCW   the nine fixed CHORDS shapes, unison first, dark ->
//                       bright
//   rows 1-6  TABLE     the module's selected chord table, unison prepended:
//     1  arpeggiate four-tone chords   as written
//     2  arpeggiate four-tone chords   folded into the octave above the root
//     3  first three tones only        as written
//     4  first three tones only        folded
//     5  omit the fifth                as written
//     6  omit the fifth                folded
// Switch tables (Original / Jon Butler / Joe McMullen) with the module's
// chord-table option. Plucked wants TRIG patched.
// Build with:
//   make -f plaits/makefile ENGINE_CONFIG=alt_firmwares/research/invented_controls/plucked_chords_ab_config.h wav

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

#define PLAITS_ENGINE_COUNT 7
#define PLAITS_BANK_SIZES { 7 }
#define PLAITS_ENGINE_ROWS { 0, 1, 2, 3, 4, 5, 6 }

#define PLAITS_HAS_SPEECH_ENGINE 0
#define PLAITS_HAS_LPC_WORDS_ENGINE 0
#define PLAITS_HAS_CHIPTUNE_ENGINE 0
#define PLAITS_HAS_USER_DATA_BANK 0
#define PLAITS_HAS_USER_DATA_BANK_OVERRIDE 0
#define PLAITS_HAS_CUSTOM_MODEL_DATA 0

#define PLAITS_ENGINE_MEMBERS \
  PluckedEngine plucked_fixed_; \
  PluckedEngine plucked_arp_written_; \
  PluckedEngine plucked_arp_folded_; \
  PluckedEngine plucked_three_written_; \
  PluckedEngine plucked_three_folded_; \
  PluckedEngine plucked_omit5_written_; \
  PluckedEngine plucked_omit5_folded_;

// Plucked goes through the LPG; gain is its catalog out/aux gain.
#define PLAITS_REGISTER_ENGINES(registry) do { \
  plucked_fixed_.set_morph_mode(PLUCKED_MORPH_CHORDS_CCW); \
  plucked_arp_written_.set_morph_mode(PLUCKED_MORPH_TABLE); \
  plucked_arp_written_.set_table_voicing( \
      PLUCKED_FOUR_TONE_ARPEGGIATE, PLUCKED_RANGE_AS_WRITTEN); \
  plucked_arp_folded_.set_morph_mode(PLUCKED_MORPH_TABLE); \
  plucked_arp_folded_.set_table_voicing( \
      PLUCKED_FOUR_TONE_ARPEGGIATE, PLUCKED_RANGE_FOLDED); \
  plucked_three_written_.set_morph_mode(PLUCKED_MORPH_TABLE); \
  plucked_three_written_.set_table_voicing( \
      PLUCKED_FOUR_TONE_FIRST_THREE, PLUCKED_RANGE_AS_WRITTEN); \
  plucked_three_folded_.set_morph_mode(PLUCKED_MORPH_TABLE); \
  plucked_three_folded_.set_table_voicing( \
      PLUCKED_FOUR_TONE_FIRST_THREE, PLUCKED_RANGE_FOLDED); \
  plucked_omit5_written_.set_morph_mode(PLUCKED_MORPH_TABLE); \
  plucked_omit5_written_.set_table_voicing( \
      PLUCKED_FOUR_TONE_OMIT_FIFTH, PLUCKED_RANGE_AS_WRITTEN); \
  plucked_omit5_folded_.set_morph_mode(PLUCKED_MORPH_TABLE); \
  plucked_omit5_folded_.set_table_voicing( \
      PLUCKED_FOUR_TONE_OMIT_FIFTH, PLUCKED_RANGE_FOLDED); \
  (registry).RegisterInstance(&plucked_fixed_, false, 1.537f, 1.537f); \
  (registry).RegisterInstance(&plucked_arp_written_, false, 1.537f, 1.537f); \
  (registry).RegisterInstance(&plucked_arp_folded_, false, 1.537f, 1.537f); \
  (registry).RegisterInstance(&plucked_three_written_, false, 1.537f, 1.537f); \
  (registry).RegisterInstance(&plucked_three_folded_, false, 1.537f, 1.537f); \
  (registry).RegisterInstance(&plucked_omit5_written_, false, 1.537f, 1.537f); \
  (registry).RegisterInstance(&plucked_omit5_folded_, false, 1.537f, 1.537f); \
} while (0)

#endif  // PLAITS_DSP_ENGINE_CONFIG_H_
