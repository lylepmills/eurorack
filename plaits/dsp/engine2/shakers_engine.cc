// Copyright 1995-2023 Perry R. Cook and Gary P. Scavone.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// STK's Shakers -- Perry Cook's PhISEM -- as one Plaits engine.

#include "plaits/dsp/engine2/shakers_engine.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/random.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Measured resonances, transcribed from STK's Shakers.cpp. Frequencies in Hz,
// radii and decays per sample at 44.1 kHz.
const float kMaracaF[] = { 3200.0f };
const float kMaracaR[] = { 0.96f };
const float kMaracaG[] = { 1.0f };

const float kCabasaF[] = { 3000.0f };
const float kCabasaR[] = { 0.7f };
const float kCabasaG[] = { 1.0f };

const float kSekereF[] = { 5500.0f };
const float kSekereR[] = { 0.6f };
const float kSekereG[] = { 1.0f };

const float kTambourineF[] = { 2300.0f, 5600.0f, 8100.0f };
const float kTambourineR[] = { 0.96f, 0.99f, 0.99f };
const float kTambourineG[] = { 0.1f, 0.8f, 1.0f };

const float kSleighF[] = { 2500.0f, 5300.0f, 6500.0f, 8300.0f, 9800.0f };
const float kSleighR[] = { 0.99f, 0.99f, 0.99f, 0.99f, 0.99f };
const float kSleighG[] = { 1.0f, 1.0f, 1.0f, 0.5f, 0.3f };

const float kBambooF[] = { 2800.0f, 2240.0f, 3360.0f };
const float kBambooR[] = { 0.995f, 0.995f, 0.995f };
const float kBambooG[] = { 1.0f, 1.0f, 1.0f };

const float kAngklungF[] = {
    1046.6f, 1174.8f, 1397.0f, 1568.0f, 1760.0f, 2093.3f, 2350.0f };
const float kAngklungR[] = {
    0.996f, 0.996f, 0.996f, 0.996f, 0.996f, 0.996f, 0.996f };
const float kAngklungG[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

const float kCokeCanF[] = { 370.0f, 1025.0f, 1424.0f, 2149.0f, 3596.0f };
const float kCokeCanR[] = { 0.99f, 0.992f, 0.992f, 0.992f, 0.992f };
const float kCokeCanG[] = { 1.0f, 1.8f, 1.8f, 1.8f, 1.8f };

const float kStixF[] = { 5500.0f };
const float kStixR[] = { 0.6f };
const float kStixG[] = { 1.0f };

const float kCrunchF[] = { 800.0f };
const float kCrunchR[] = { 0.95f };
const float kCrunchG[] = { 1.0f };

const float kBigRocksF[] = { 6460.0f };
const float kBigRocksR[] = { 0.932f };
const float kBigRocksG[] = { 1.0f };

const float kLittleRocksF[] = { 9000.0f };
const float kLittleRocksR[] = { 0.843f };
const float kLittleRocksG[] = { 1.0f };

// Mug plus a quarter: four mug modes then three coin modes, which is how
// upstream builds every mug-and-coin variant. The other five coins differ only
// in those last three numbers, so one of them stands for the family.
const float kMugF[] = {
    2123.0f, 4518.0f, 8856.0f, 10753.0f, 1708.0f, 8863.0f, 9045.0f };
const float kMugR[] = {
    0.997f, 0.997f, 0.997f, 0.997f, 0.9995f, 0.9995f, 0.9995f };
const float kMugG[] = { 1.0f, 0.8f, 0.6f, 0.4f, 1.0f, 0.8f, 0.5f };

const float kWaterF[] = { 450.0f, 600.0f, 750.0f };
const float kWaterR[] = { 0.9985f, 0.9985f, 0.9985f };
const float kWaterG[] = { 1.0f, 1.0f, 1.0f };

const float kGuiroF[] = { 2500.0f, 4000.0f };
const float kGuiroR[] = { 0.97f, 0.97f };
const float kGuiroG[] = { 1.0f, 1.0f };

const float kWrenchF[] = { 3200.0f, 8000.0f };
const float kWrenchR[] = { 0.99f, 0.992f };
const float kWrenchG[] = { 1.0f, 1.0f };

enum ShakerMechanism {
  SHAKER_MECHANISM_SHAKE = 0,
  SHAKER_MECHANISM_RATCHET = 1,
  SHAKER_MECHANISM_WATER = 2
};

// Rate-corrects a per-sample decay or filter radius from 44.1 kHz to 48 kHz so
// its time constant, not its per-sample value, is preserved.
inline float RateCorrect(float coefficient) {
  if (coefficient <= 0.0f) {
    return 0.0f;
  }
  if (coefficient >= 1.0f) {
    return 1.0f;
  }
  return powf(coefficient, kShakersReferenceRate / kSampleRate);
}

inline float Noise() {
  return 2.0f * Random::GetFloat() - 1.0f;
}

}  // namespace

const ShakerPreset kShakerPresets[kShakersNumInstruments] = {
  //                        n  freq          radii         gains       objects  sysdec  snddec  gain  dscale vary  mask  b0     b1     b2    mech                       ratchet
  { "Maraca",               1, kMaracaF,      kMaracaR,      kMaracaG,      25.0f, 0.999f,  0.95f,  4.0f, 0.97f, 0.0f,  0x00, 1.0f, -1.0f,  0.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Cabasa",               1, kCabasaF,      kCabasaR,      kCabasaG,     512.0f, 0.997f,  0.96f,  8.0f, 0.97f, 0.0f,  0x00, 1.0f, -1.0f,  0.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Sekere",               1, kSekereF,      kSekereR,      kSekereG,      64.0f, 0.999f,  0.96f,  4.0f, 0.94f, 0.0f,  0x00, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Tambourine",           3, kTambourineF,  kTambourineR,  kTambourineG,  32.0f, 0.9985f, 0.95f,  1.0f, 0.95f, 0.05f, 0x06, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Sleigh bells",         5, kSleighF,      kSleighR,      kSleighG,      32.0f, 0.9994f, 0.97f,  1.0f, 0.9f,  0.03f, 0x1f, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Bamboo chimes",        3, kBambooF,      kBambooR,      kBambooG,       1.2f, 0.9999f, 0.9f,   0.4f, 0.7f,  0.2f,  0x07, 1.0f,  0.0f,  0.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Angklung",             7, kAngklungF,    kAngklungR,    kAngklungG,     1.2f, 0.9999f, 0.95f,  0.5f, 0.7f,  0.0f,  0x00, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Coke can",             5, kCokeCanF,     kCokeCanR,     kCokeCanG,     48.0f, 0.999f,  0.97f,  0.5f, 0.95f, 0.0f,  0x00, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Sticks",               1, kStixF,        kStixR,        kStixG,         2.0f, 0.998f,  0.96f,  6.0f, 0.96f, 0.0f,  0x00, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Crunch",               1, kCrunchF,      kCrunchR,      kCrunchG,       7.0f, 0.99806f,0.95f,  4.0f, 0.96f, 0.0f,  0x00, 1.0f, -1.0f,  0.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Big rocks",            1, kBigRocksF,    kBigRocksR,    kBigRocksG,    23.0f, 0.9965f, 0.98f,  4.0f, 0.95f, 0.11f, 0x01, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Little rocks",         1, kLittleRocksF, kLittleRocksR, kLittleRocksG,1600.0f,0.99586f,0.98f,  4.0f, 0.95f, 0.18f, 0x01, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Coins in a mug",       7, kMugF,         kMugR,         kMugG,          3.0f, 0.9995f, 0.97f,  0.8f, 0.95f, 0.0f,  0x00, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_SHAKE,   0.0f },
  { "Water drops",          3, kWaterF,       kWaterR,       kWaterG,       10.0f, 0.996f,  0.95f,  1.0f, 0.8f,  0.0f,  0x00,-1.0f,  0.0f,  1.0f, SHAKER_MECHANISM_WATER,   0.0f },
  { "Guiro",                2, kGuiroF,       kGuiroR,       kGuiroG,      128.0f, 0.999f,  0.95f,  0.4f, 0.95f, 0.0f,  0x00, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_RATCHET, kShakersGuiroRatchetDelta },
  { "Wrench",               2, kWrenchF,      kWrenchR,      kWrenchG,     128.0f, 0.999f,  0.95f,  0.4f, 0.95f, 0.0f,  0x00, 1.0f,  0.0f, -1.0f, SHAKER_MECHANISM_RATCHET, kShakersWrenchRatchetDelta },
};

void ShakersEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  instrument_ = -1;
  note_ratio_ = 1.0f;
  Reset();
}

void ShakersEngine::Reset() {
  shake_energy_ = 0.0f;
  sound_level_ = 0.0f;
  ratchet_count_ = 0.0f;
  ratchet_delta_ = kShakersGuiroRatchetDelta;
  eq_x1_ = 0.0f;
  eq_x2_ = 0.0f;
  for (int i = 0; i < kShakersMaxResonances; ++i) {
    y1_[i] = 0.0f;
    y2_[i] = 0.0f;
  }
  const int instrument = instrument_ < 0 ? 0 : instrument_;
  instrument_ = -1;
  SetInstrument(instrument);
}

void ShakersEngine::SetInstrument(int instrument) {
  if (instrument == instrument_) {
    return;
  }
  instrument_ = instrument;
  const ShakerPreset& p = kShakerPresets[instrument];

  for (int i = 0; i < p.num_resonances; ++i) {
    frequency_[i] = p.frequencies[i];
    radius_[i] = RateCorrect(p.radii[i]);
    // Upstream's resonators are all-pole with a numerator of 1, so their peak
    // gain is about 1/(1-r^2) -- which runs from 1.6 for a sekere's r=0.6 to
    // 125 for an angklung's r=0.996. Cook's per-instrument gains only partly
    // offset that (the product still spans 6 to 62), because STK expects a
    // master gain downstream and his own commented-out debug line in `tick`
    // is a check for output exceeding 1.0. Normalising each resonator to unit
    // peak here is what lets the sixteen instruments sit at comparable levels
    // on one engine, and it leaves each instrument's gain meaning what it
    // reads as: its intended loudness.
    normalization_[i] = 1.0f - radius_[i] * radius_[i];
    gain_[i] = p.gains[i];
    y1_[i] = 0.0f;
    y2_[i] = 0.0f;
    water_frequency_[i] = p.frequencies[i];
  }
  // A water drop starts silent and is born by the mechanism.
  if (p.mechanism == SHAKER_MECHANISM_WATER) {
    for (int i = 0; i < p.num_resonances; ++i) {
      gain_[i] = 0.0f;
    }
  }
  shake_energy_ = 0.0f;
  sound_level_ = 0.0f;
  ratchet_count_ = 0.0f;
  ratchet_delta_ = p.ratchet_delta;
  eq_x1_ = 0.0f;
  eq_x2_ = 0.0f;
}

void ShakersEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  // PhISEM carries its own energy envelope; a second one from the LPG would
  // swallow the decay that identifies the instrument.
  *already_enveloped = true;

  int instrument = static_cast<int>(
      parameters.harmonics * kShakersNumInstruments);
  CONSTRAIN(instrument, 0, kShakersNumInstruments - 1);
  SetInstrument(instrument);

  const ShakerPreset& p = kShakerPresets[instrument];

  // MACRO is upstream's object-count control verbatim, and its own mapping
  // already puts the measured value at the detent: at 0.5 this is baseObjects.
  num_objects_ = 2.0f * parameters.macro * p.num_objects + 1.1f;
  current_gain_ = logf(num_objects_) * p.gain / num_objects_;
  // 48 kHz gives 8.8% more chances per second for a collision than the rate
  // these counts were tuned at, so the threshold is scaled to hold the
  // real-time density.
  const float object_threshold = num_objects_ *
      (kShakersReferenceRate / kSampleRate);

  // MORPH is upstream's decay control, likewise centred on the measured value.
  const float base_decay = RateCorrect(p.system_decay);
  system_decay_ = base_decay + 2.0f * (parameters.morph - 0.5f) *
      p.decay_scale * (1.0f - base_decay);
  CONSTRAIN(system_decay_, 0.0f, 0.99999f);
  const float sound_decay = RateCorrect(p.sound_decay);

  // The note transposes every resonance. Retuning per block rather than per
  // collision is what makes this affordable.
  note_ratio_ = SemitonesToRatio(parameters.note - kShakersReferenceNote);
  for (int i = 0; i < p.num_resonances; ++i) {
    float f = frequency_[i] * note_ratio_;
    CONSTRAIN(f, 1.0f, 0.49f * kSampleRate);
    a1_[i] = -2.0f * radius_[i] * cosf(2.0f * float(M_PI) * f / kSampleRate);
    a2_[i] = radius_[i] * radius_[i];
  }

  // TIMBRE is how hard it is being shaken. Upstream injects energy from a MIDI
  // controller; here the knob injects continuously so the engine drones with
  // TRIG unpatched, and a rising edge is a single hard shake.
  const float shake_rate = parameters.timbre * parameters.timbre * 0.02f;
  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    if (p.mechanism == SHAKER_MECHANISM_RATCHET) {
      ratchet_count_ += 8.0f;
      ratchet_delta_ = p.ratchet_delta * (1.0f + 8.0f * parameters.timbre);
    } else {
      shake_energy_ = kShakersMaxShake;
    }
  }

  for (size_t i = 0; i < size; ++i) {
    float input = 0.0f;

    if (p.mechanism == SHAKER_MECHANISM_RATCHET) {
      // A ratchet ramps its energy down per tooth and resets, which is the
      // rasp; it does not decay away like a shaken instrument.
      if (shake_rate > 0.0f && ratchet_count_ < 1.0f) {
        ratchet_count_ = 1.0f;
        ratchet_delta_ = p.ratchet_delta * (1.0f + 8.0f * parameters.timbre);
      }
      if (ratchet_count_ > 0.0f) {
        shake_energy_ -= ratchet_delta_ + 0.002f * shake_energy_;
        if (shake_energy_ < 0.0f) {
          shake_energy_ = 1.0f;
          ratchet_count_ -= 1.0f;
        }
        if (Random::GetFloat() * 1024.0f < object_threshold) {
          sound_level_ += shake_energy_ * shake_energy_;
        }
        input = sound_level_ * Noise() * shake_energy_;
      }
    } else {
      shake_energy_ += shake_rate * kShakersMaxShake * 0.1f;
      if (shake_energy_ > kShakersMaxShake) {
        shake_energy_ = kShakersMaxShake;
      }
      if (shake_energy_ < kShakersMinEnergy) {
        shake_energy_ = 0.0f;
      }
      shake_energy_ *= system_decay_;

      if (p.mechanism == SHAKER_MECHANISM_WATER) {
        // Each drop is born into whichever resonator has fully decayed, tuned
        // around the middle resonance and then swept upward as it rings.
        if (Random::GetFloat() * 32767.0f < object_threshold) {
          sound_level_ = shake_energy_;
          const int j = static_cast<int>(Random::GetFloat() * 3.0f) % 3;
          for (int k = 0; k < 3; ++k) {
            const int slot = (j + k) % 3;
            if (gain_[slot] == 0.0f) {
              const float centre = p.frequencies[1] *
                  (0.75f + 0.25f * float(slot) + 0.25f * Noise());
              water_frequency_[slot] = centre;
              gain_[slot] = fabsf(Noise());
              break;
            }
          }
        }
        for (int k = 0; k < p.num_resonances; ++k) {
          gain_[k] *= radius_[k];
          if (gain_[k] > 0.001f) {
            water_frequency_[k] *= kShakersWaterFreqSweep;
            float f = water_frequency_[k] * note_ratio_;
            CONSTRAIN(f, 1.0f, 0.49f * kSampleRate);
            a1_[k] = -2.0f * radius_[k] *
                cosf(2.0f * float(M_PI) * f / kSampleRate);
          } else {
            gain_[k] = 0.0f;
          }
        }
        input = sound_level_;
      } else if (Random::GetFloat() * 1024.0f < object_threshold) {
        sound_level_ += shake_energy_;
        input = sound_level_;
        // Instruments whose colliding objects are themselves resonant --
        // timbrels, bells, bamboo, gravel -- jitter their resonances on each
        // collision, which is what stops them sounding like one struck object.
        if (p.vary_factor > 0.0f) {
          for (int k = 0; k < p.num_resonances; ++k) {
            if (p.vary_mask & (1 << k)) {
              float f = frequency_[k] * note_ratio_ *
                  (1.0f + p.vary_factor * Noise());
              CONSTRAIN(f, 1.0f, 0.49f * kSampleRate);
              a1_[k] = -2.0f * radius_[k] *
                  cosf(2.0f * float(M_PI) * f / kSampleRate);
            }
          }
        }
      }
    }

    sound_level_ *= sound_decay;

    // AUX carries the bare collision train before any resonance -- the impact
    // layer, useful under a different body or as a trigger source. Scaled by
    // the same object gain that feeds the resonators, because a dense
    // instrument's sound level accumulates well past 1.0 on its own.
    aux[i] = SoftClip(input * current_gain_);

    float summed = 0.0f;
    for (int k = 0; k < p.num_resonances; ++k) {
      const float y = input * gain_[k] * current_gain_ * normalization_[k]
          - a1_[k] * y1_[k] - a2_[k] * y2_[k];
      y2_[k] = y1_[k];
      y1_[k] = y;
      summed += y;
    }

    // The equalizer is where the zeros live. Upstream's three numerator sets
    // are (1, -1, 0), (1, 0, -1) and (-1, 0, 1) -- a one-sample difference, a
    // two-sample difference, and its inversion -- and they are a large part of
    // why a cabasa is dry and a coke can is not.
    const float equalized = p.eq_b0 * summed + p.eq_b1 * eq_x1_ +
        p.eq_b2 * eq_x2_;
    eq_x2_ = eq_x1_;
    eq_x1_ = summed;

    // The equalizer's two-sample difference can add 6 dB on its own, and a
    // hard-shaken dense instrument stacks several resonators in phase.
    out[i] = SoftClip(equalized);
  }
}

}  // namespace plaits
