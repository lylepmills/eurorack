// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_DSP_INTEGRATED_WAVETABLE_H_
#define PLAITS_DSP_INTEGRATED_WAVETABLE_H_

#include <stddef.h>
#include <stdint.h>

#include "plaits/build_config.h"
#include "plaits/resources.h"

namespace plaits {

const size_t kIntegratedWavetableSize = 128;
const size_t kIntegratedWavetableStride = kIntegratedWavetableSize + 4;

// Keep the three Mutable banks in separate linker sections. Generated Palette
// configurations can therefore retain exactly the factory banks still named by
// Wavetable, Wave Terrain, Chords, or the Braids-derived wavetable models.
inline const int16_t* FactoryIntegratedWavetableBank(int bank) {
  switch (bank) {
#if PLAITS_WAVETABLE_FACTORY_MASK & 0x01
    case 0:
      return wav_integrated_waves_1;
#endif
#if PLAITS_WAVETABLE_FACTORY_MASK & 0x02
    case 1:
      return wav_integrated_waves_2;
#endif
#if PLAITS_WAVETABLE_FACTORY_MASK & 0x04
    case 2:
      return wav_integrated_waves_3;
#endif
    default:
      return NULL;
  }
}

inline const int16_t* FactoryIntegratedWavetable(int bank, int frame) {
  const int16_t* data = FactoryIntegratedWavetableBank(bank);
  return data ? data + size_t(frame) * kIntegratedWavetableStride : NULL;
}

inline const int16_t* FactoryIntegratedWavetable(int wave) {
  return FactoryIntegratedWavetable(wave / 64, wave & 63);
}

// The same lookup spelled as an ADDRESS CONSTANT, for namespace-scope tables.
// A table initialized by calling the functions above is DYNAMICALLY
// initialized: the compiler emits a static constructor into .init_array, which
// the linker script KEEPs, so the table and every bank it names link into
// every build -- whether or not its engine is placed. That silently re-linked
// two 16,896 B banks into every palette without Chords. An address constant
// is initialized statically in .rodata, which --gc-sections reclaims together
// with the banks once nothing reaches the table. `bank` must be a literal 0-2.
#if PLAITS_WAVETABLE_FACTORY_MASK & 0x01
#define PLAITS_FACTORY_WAVE_0(frame) \
  (wav_integrated_waves_1 + (frame) * kIntegratedWavetableStride)
#else
#define PLAITS_FACTORY_WAVE_0(frame) NULL
#endif
#if PLAITS_WAVETABLE_FACTORY_MASK & 0x02
#define PLAITS_FACTORY_WAVE_1(frame) \
  (wav_integrated_waves_2 + (frame) * kIntegratedWavetableStride)
#else
#define PLAITS_FACTORY_WAVE_1(frame) NULL
#endif
#if PLAITS_WAVETABLE_FACTORY_MASK & 0x04
#define PLAITS_FACTORY_WAVE_2(frame) \
  (wav_integrated_waves_3 + (frame) * kIntegratedWavetableStride)
#else
#define PLAITS_FACTORY_WAVE_2(frame) NULL
#endif
#define PLAITS_FACTORY_WAVE(bank, frame) PLAITS_FACTORY_WAVE_##bank(frame)

}  // namespace plaits

#endif  // PLAITS_DSP_INTEGRATED_WAVETABLE_H_
