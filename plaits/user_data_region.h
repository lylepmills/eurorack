// Copyright 2026 Lyle Mills.
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
// Rewritable user-data region descriptor.
//
// Its own header because the generated engine_config.h defines the region TABLE
// and is force-included ahead of everything else, while user_data.h — which
// reads that table from inside UserData's member functions — is parsed later in
// the translation unit. Both need the type; only one can define it.

#ifndef PLAITS_USER_DATA_REGION_H_
#define PLAITS_USER_DATA_REGION_H_

#include "stmlib/stmlib.h"

namespace plaits {

// The flash one FM bank occupies, which an audio transfer erases and reprograms
// in place. Custom Terrain/Wavetable regions use their direct per-engine pointer
// table instead; both kinds share the page-aligned linker output section and
// linked-ELF safety check.
//
// In a Plaits Palette build a bank's BAKED array IS its region: the generator
// emits every bank 2 KB page-aligned and padded to a whole UserData::SIZE, so a
// region always covers whole flash pages and nothing else ever shares them.
// That is a correctness requirement, not tidiness — UserData::Save erases those
// pages, so a region overlapping code or rodata would destroy the firmware on
// the first transfer. check_user_data_regions.py asserts it against the linked
// ELF, and the build fails if it does not hold.
//
// Two consequences are worth stating, because they delete work rather than add
// it. An un-transferred slot needs no empty state at all — it simply reads its
// baked bank, exactly as a non-swappable slot does. And a firmware update
// restores every bank uniformly, because the banks travel inside the image.
//
// Regions are keyed on BANK, not slot: two placed slots may map to one factory
// bank (kEngineUserDataBank) and share its flash, and both must see a transfer
// made through either.
struct UserDataRegion {
  const uint8_t* data;
  int bank;
};

}  // namespace plaits

#endif  // PLAITS_USER_DATA_REGION_H_
