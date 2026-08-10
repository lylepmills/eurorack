// Copyright 2021 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// User data manager.

#ifndef PLAITS_USER_DATA_H_
#define PLAITS_USER_DATA_H_

#include "stmlib/stmlib.h"

#include "plaits/user_data_region.h"

#ifdef TEST

#include <cstdio>

// Mock flash saving functions for debugging purposes.
#define PAGE_SIZE 0x800

inline void FLASH_Unlock() { }

inline void FLASH_ErasePage(uint32_t address) {
  printf("--- Erasing page at %08x ---\n", address);
}

inline void FLASH_ProgramWord(uint32_t address, uint32_t word) {
  if (address % 32 == 0) {
    printf("%08x ", address);
  }
  printf(
      "%08x ",
      (word >> 24) | ((word & 0x00ff0000) >> 8) | ((word & 0x0000ff00) << 8) | (word << 24));
  if (address % 32 == 28) {
    printf("\n");
  }
}

#else

#include <stm32f37x_conf.h>
#include "stmlib/system/flash_programming.h"

#endif  // TEST

namespace plaits {

class UserData {
 public:
  enum {
    // Stock / default firmware keeps its single region in the bootloader's own
    // area, below the application. Generated custom Wavetable slots, shared
    // terrain-bank entries, and replaceable-FM banks use dedicated application-
    // flash regions; all others keep this one.
    ADDRESS = 0x08007000,
    SIZE = 0x1000
  };

  UserData() { }
  ~UserData() { }

  inline const uint8_t* ptr(int slot) const {
    const uint8_t* data = region(slot);
    if (!data) {
      return NULL;
    }
#ifdef TEST
    // A host build has no flash behind the legacy address, so reading the tag
    // there would fault. Swappable bank regions are ordinary arrays and are read
    // normally, which is what user_data_region_test exercises.
    if (data == (const uint8_t*)(ADDRESS)) {
      return NULL;
    }
#endif  // TEST
    // The tag distinguishes a TRANSFERRED bank from the baked bank sitting in
    // the same region. The generator clears these two bytes in every baked bank
    // (they are the tail of voice 32's name field, which Plaits never displays)
    // and re-emits a live stock bank rather than leave one whose own tail might
    // alias, so a baked bank can never be mistaken for a transferred one.
    if (data[SIZE - 2] == 'U' && data[SIZE - 1] == (' ' + tag_of(slot))) {
      return data;
    } else {
      return NULL;
    }
  }

  // Bytes of packed patch data behind a TRANSFERRED bank.
  //
  // The 4096 bytes are fully occupied by 32 packed voices, so a bank that holds
  // fewer has to declare it somewhere; the count lives in the tail of voice 32's
  // name field, which Plaits never displays. Without it every transferred bank
  // reads as a full 32 — an 8-patch pick list had to repeat four times to fill
  // the bank, and HARMONICS swept 8 zones of 4 instead of 8 steps, visibly
  // contradicting the short BAKED banks shipped since recipe schema v13.
  //
  //   SIZE-4  0xa5   magic
  //   SIZE-3  count  1..32
  //   SIZE-2  'U'    tag (written by Save)
  //   SIZE-1  bank   tag (written by Save)
  //
  // The magic is not optional. Without it, a payload from an encoder that
  // predates the count carries an arbitrary name character at SIZE-3; printable
  // ASCII is 32..127, so a stray 1..31 would be read as a short bank. Requiring
  // 0xa5 makes "absent" unambiguous, so an OLD file on NEW firmware still reads
  // as 32 — and Save rewrites only SIZE-2/SIZE-1, so both count bytes survive
  // into flash untouched.
  static inline size_t bank_length(const uint8_t* data) {
    const uint8_t kCountMagic = 0xa5;
    const size_t kPackedPatchSize = 128;  // fm::Patch::SYX_SIZE
    const int kMaxPatchesPerBank = SIZE / kPackedPatchSize;
    if (data && data[SIZE - 4] == kCountMagic) {
      const int count = data[SIZE - 3];
      if (count >= 1 && count <= kMaxPatchesPerBank) {
        return count * kPackedPatchSize;
      }
    }
    return SIZE;
  }

  inline bool Save(uint8_t* rx_buffer, int slot, float harmonics = 0.0f) {
    if (slot < rx_buffer[SIZE - 2] || slot > rx_buffer[SIZE - 1]) {
      return false;
    }

    // A shared custom-terrain entry, dedicated custom-model slot, or replaceable
    // FM bank writes its own region; every other slot retains the module's
    // historical single legacy region. If a generated mapping explicitly
    // refuses a slot, nothing is erased.
    const uint8_t* destination = NULL;
    int tag = tag_of(slot);
#if defined(PLAITS_HAS_TERRAIN_BANK) && PLAITS_HAS_TERRAIN_BANK
    if ((kWaveTerrainEngineMask & (1u << slot)) != 0) {
      float position = harmonics * 1.05f;
      if (position < 0.0f) position = 0.0f;
      if (position > 1.0f) position = 1.0f;
      int terrain = static_cast<int>(
          position * static_cast<float>(kTerrainBank.size - 1.0001f) + 0.5f);
      if (terrain < 0) terrain = 0;
      if (terrain >= static_cast<int>(kTerrainBank.size)) {
        terrain = static_cast<int>(kTerrainBank.size) - 1;
      }
      if (kTerrainBank.types[terrain] != WAVE_TERRAIN_CUSTOM
          || !kTerrainBank.data[terrain]) {
        // Factory terrains are code/resources, not erase-safe data pages.
        return false;
      }
      destination = reinterpret_cast<const uint8_t*>(kTerrainBank.data[terrain]);
      tag = 32 + terrain;
    } else
#endif  // PLAITS_HAS_TERRAIN_BANK
    {
      destination = region(slot);
    }
    if (!destination) {
      return false;
    }

    // Tag the data to identify which bank it should be associated to.
    rx_buffer[SIZE - 2] = 'U';
    rx_buffer[SIZE - 1] = ' ' + tag;

    // Write to FLASH. uintptr_t rather than uint32_t so the host test can build:
    // on the target the two are the same type, but a 64-bit host pointer does
    // not fit in uint32_t.
    const uintptr_t address = reinterpret_cast<uintptr_t>(destination);
    const uint32_t* words = static_cast<const uint32_t*>(
        static_cast<const void*>(rx_buffer));
    for (uintptr_t i = address; i < address + SIZE; i += 4) {
      if (i % PAGE_SIZE == 0) {
        FLASH_Unlock();
        FLASH_ErasePage(static_cast<uint32_t>(i));
      }
      FLASH_ProgramWord(static_cast<uint32_t>(i), *words++);
    }
    return true;
  }

 private:
  // The user-data area is GENERIC, not an FM feature: SixOpEngine reads it as a
  // patch bank, WavetableEngine as wave indices plus custom wave data, and
  // WaveTerrainEngine as a ninth terrain. Generated builds can now carry three
  // kinds of dedicated region:
  //
  //   custom entry in the shared terrain bank    -> that entry's seeded array
  //   customized legacy Terrain/Wavetable slot   -> that slot's seeded array
  //   FM slot in a build with REPLACEABLE banks  -> that bank's baked array
  //   everything else                            -> legacy region at ADDRESS
  //
  // A custom-model assignment is itself the opt-in: its 4 KB recipe payload is
  // page-aligned and doubles as the flash region that future TIMBRE transfers
  // erase and rewrite. Uncustomized Terrain/Wavetable slots keep the legacy
  // area, so old transfer files and builds behave exactly as before.
#if defined(PLAITS_HAS_USER_DATA_BANK) && PLAITS_HAS_USER_DATA_BANK
  inline int bank_of(int slot) const { return kEngineUserDataBank[slot]; }
#else
  inline int bank_of(int slot) const { (void) slot; return -1; }
#endif

  // What a region's tag identifies. An FM region is keyed on the BANK the slot
  // maps to, so two slots sharing one factory bank both read a transfer made
  // through either; the legacy region is keyed on the slot, as it always was.
  inline int tag_of(int slot) const {
#if defined(PLAITS_HAS_CUSTOM_MODEL_DATA) && PLAITS_HAS_CUSTOM_MODEL_DATA
    if (kEngineCustomModelData[slot]) {
      return slot;
    }
#endif
#if defined(PLAITS_HAS_SWAPPABLE_USER_DATA_BANKS) \
    && PLAITS_HAS_SWAPPABLE_USER_DATA_BANKS
    const int bank = bank_of(slot);
    if (bank >= 0) {
      return bank;
    }
#endif
    return slot;
  }

  // The flash a slot may be transferred into, or NULL if it owns none.
  //
  // The mapping is STATIC — Plaits Palette knows at build time which banks the
  // firmware carries, so the generator bakes the table. That deletes an entire
  // category of design: no dynamic allocation, no eviction policy, no "banks
  // full" state, no user-facing choice of where a transfer lands.
  inline const uint8_t* region(int slot) const {
#if defined(PLAITS_HAS_CUSTOM_MODEL_DATA) && PLAITS_HAS_CUSTOM_MODEL_DATA
    // Custom model regions are keyed by ENGINE SLOT, not by engine instance or
    // payload digest. Two identical seeds still own different flash pages so a
    // later transfer can change one without changing the other.
    if (kEngineCustomModelData[slot]) {
      return kEngineCustomModelData[slot];
    }
#endif
#if defined(PLAITS_HAS_SWAPPABLE_USER_DATA_BANKS) \
    && PLAITS_HAS_SWAPPABLE_USER_DATA_BANKS
    const int bank = bank_of(slot);
    if (bank >= 0) {
      for (int i = 0; i < kNumUserDataRegions; ++i) {
        if (kUserDataRegions[i].bank == bank) {
          return kUserDataRegions[i].data;
        }
      }
      // An FM bank the generator stripped: no slot reads it, so there is nothing
      // to replace. Refused rather than sent to the legacy region, which would
      // accept the transfer into an area this build never reads and so appear to
      // succeed while doing nothing.
      return NULL;
    }
#endif  // PLAITS_HAS_SWAPPABLE_USER_DATA_BANKS
    return (const uint8_t*)(ADDRESS);
  }
};

}  // namespace plaits

#endif  // PLAITS_USER_DATA_H_
