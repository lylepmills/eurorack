// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' QPSK model: a carrier driven by a framed packet of dibits.

#include "plaits/dsp/engine2/digital_modulation_engine.h"
#include "plaits/build_config.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' kConstellationI / kConstellationQ, as signs on the radius. The
// table is four points at +-R in quadrature, so it reduces to sign lookups
// and no table is carried across.
inline float ConstellationI(int dibit) {
  return (dibit == 0 || dibit == 1) ? 1.0f : -1.0f;
}

inline float ConstellationQ(int dibit) {
  return (dibit == 0 || dibit == 3) ? 1.0f : -1.0f;
}

// Braids' wav_sine, verbatim: fitted against braids/resources.cc's 257-entry
// table (least-squares against -cos(2*pi*i/256), max residual 1.68 LSB, i.e.
// the table's own int16 quantization noise, not a modeling error), it is
// 32639 * -cos(2*pi*x) + 127 -- NOT the unit-amplitude, zero-mean -cos this
// used to compute.
inline float BraidsSine(float phase) {
  return (32639.0f / 32768.0f) * Sine(phase + 0.75f) + (127.0f / 32768.0f);
}

// Braids' ToParameter equivalent: the payload knob arrives as an int16
// 0..32767 (braids.cc:219-224 CONSTRAIN) and the packet reads only its top 8
// bits, so the port has to enter the same integer or it transmits a different
// byte. See the header's PAYLOAD IS AN INTEGER PIPELINE note.
inline int32_t ToBraidsParameter(float normalized) {
  int32_t value = static_cast<int32_t>(normalized * 32767.0f);
  CONSTRAIN(value, 0, 32767);
  return value;
}

}  // namespace

void DigitalModulationEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void DigitalModulationEngine::Reset() {
  phase_ = 0.0f;
  symbol_phase_ = 0.0f;
  // Braids' DigitalModulationState lives in the `state_` union that
  // DigitalOscillator::Init() memsets (digital_oscillator.h:246-247), and
  // Init() runs on every shape change (digital_oscillator.cc:110-115) -- the
  // same cadence at which Plaits calls Reset() on an engine switch
  // (voice.cc:179). So a fresh selection ALWAYS starts with filter_state 0 and
  // climbs to the knob from there; seeding the noon byte instead would fake a
  // packet Braids never transmits.
  payload_filter_ = 0;
  symbol_count_ = 0;
  data_byte_ = 0;
  shaped_i_ = 0.0f;
  shaped_q_ = 0.0f;
  dc_aux_in_ = 0.0f;
  dc_aux_out_ = 0.0f;
}

void DigitalModulationEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    // Braids' strike resets the symbol count ONLY (digital_oscillator.cc:
    // 2196-2199): the packet restarts from its preamble while the carrier, the
    // payload filter AND the byte in flight all run on. Counts 1, 2 and 3 take
    // the shift branch, so the three symbols after a strike are the remnant of
    // whatever byte was being transmitted, and the preamble's 0x00 only latches
    // at count 4. Zeroing data_byte_ here would silence those three.
    symbol_count_ = 0;
  }

  const float carrier_increment = NoteToFrequency(parameters.note);

  // TIMBRE is Braids' symbol rate: an octave below the note, then up to
  // another 32 semitones below that.
  const float symbol_note = parameters.note + \
      kDigitalModulationSymbolOffset - \
      kDigitalModulationSymbolRange * (1.0f - parameters.timbre);
  const float symbol_increment = NoteToFrequency(symbol_note);

  // HARMONICS scales the whole packet; the preamble and both sync words keep
  // their proportion of it, so the structure survives at every frame length.
  // The law is EXPONENTIAL, not linear. Braids' header is 64 of 1,088 symbols, and the
  // port keeps that proportion -- so a linear frame law leaves the header
  // tens of symbols long across most of the knob, and at a few tens of Hz of
  // symbol rate the payload is seconds away. Everything interesting lives at
  // short frames, so the knob should spend its travel there: HARMONICS = 1 is
  // still Braids' 1,088 exactly, but noon is ~187 rather than 560.
  //
  // The +1 on each boundary is a floor so the shortest frames still carry a
  // preamble group, and at HARMONICS = 1 it is ALSO exactly Braids. Do not
  // "correct" it away from an exp2 calculation: stmlib's SemitonesToRatio is a
  // LUT with a TRUNCATED 1/256-semitone fractional index, so the full span
  // returns 1087.83, not exp2's 1088.03. The unbiased ints are therefore
  // 31/47/63/1087 and the +1 lands them on 32/48/64/1088 -- Braids' literals at
  // digital_oscillator.cc:2205-2215 (frame 64 + 4 * 256). Measured, and the
  // dibit streams diff to zero through the header and across the frame wrap.
  // Only multiples of 4 ever reach these comparisons, so a boundary is really a
  // GROUP index and 31 and 32 select the same groups either way.
  const float frame = kDigitalModulationMinFrame * \
      SemitonesToRatio(parameters.harmonics * kDigitalModulationFrameSpan);
  const float frame_scale = frame / kDigitalModulationStockFrame;
  const int preamble_end = static_cast<int>(
      kDigitalModulationPreamble * frame_scale) + 1;
  const int sync_a_end = static_cast<int>(
      kDigitalModulationSyncA * frame_scale) + 1;
  const int sync_b_end = static_cast<int>(
      kDigitalModulationSyncB * frame_scale) + 1;
  const int frame_end = static_cast<int>(frame) + 1;

  // MACRO is Braids' COLOR knob, and it enters the packet as an int16 the
  // filter chases -- NOT as a float byte. The detent is the module's noon, so
  // it lands on 16383 >> 7 = 127 rather than a rounded 128.
  const int32_t payload = ToBraidsParameter(parameters.macro);

  // MORPH shapes the symbol transitions. At zero the constellation points are
  // hard steps, which is Braids; above it they glide.
  const float shaping = parameters.morph;
  const float shape_pole = shaping * shaping * 0.9995f;

  const bool stereo = PLAITS_STEREO_DIGITAL_MODULATION && parameters.stereo;

  for (size_t i = 0; i < size; ++i) {
    float carrier_frequency = carrier_increment;
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
    if (parameters.frequency_offset) {
      carrier_frequency += parameters.frequency_offset[i];
      CONSTRAIN(carrier_frequency, -0.5f, 0.499999f);
    }
#endif
    phase_ += carrier_frequency;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    } else if (phase_ < 0.0f) {
      phase_ += 1.0f;
    }
    symbol_phase_ += symbol_increment;
    if (symbol_phase_ >= 1.0f) {
      symbol_phase_ -= 1.0f;
      ++symbol_count_;
      if (!(symbol_count_ & 3)) {
        if (symbol_count_ >= frame_end) {
          symbol_count_ = 0;
        }
        if (symbol_count_ < preamble_end) {
          data_byte_ = 0x00;
        } else if (symbol_count_ < sync_a_end) {
          data_byte_ = 0x99;
        } else if (symbol_count_ < sync_b_end) {
          data_byte_ = 0xcc;
        } else {
          // Braids' payload one-pole, verbatim: an int32 accumulator, a 3/4
          // pole taken as an arithmetic >>2 (which FLOORS, so the climb lags a
          // float pole and parks one to three LSB short of the knob), and a
          // >>7 that keeps only the top 8 bits. The byte IS the dibit
          // sequence, so a one-LSB difference here is a different
          // transmission, not a rounding error.
          payload_filter_ = (payload_filter_ * 3 + payload) >> 2;
          data_byte_ = static_cast<uint8_t>(payload_filter_ >> 7);
        }
      } else {
        // Shift out the dibit just transmitted.
        data_byte_ >>= 2;
      }
    }

    const int dibit = data_byte_ & 3;
    const float target_i = kDigitalModulationRadius * ConstellationI(dibit);
    const float target_q = kDigitalModulationRadius * ConstellationQ(dibit);
    shaped_i_ += (target_i - shaped_i_) * (1.0f - shape_pole);
    shaped_q_ += (target_q - shaped_q_) * (1.0f - shape_pole);

    const float in_phase = BraidsSine(phase_);
    const float quadrature = BraidsSine(phase_ + 0.25f);

    if (stereo) {
      // L/R: the two quadrature components, one per channel. They are the
      // constituents OUT is built from, so L + R reproduces the mono output
      // EXACTLY -- the split is a decomposition, not a second render, and it
      // is perfectly mono-compatible. Each channel therefore peaks at the
      // constellation radius against the mono 0.997, i.e. 3 dB down; two
      // decorrelated channels at -3 dB carry the same power as one at 0, so
      // no make-up is applied (and applying one would break the identity).
      out[i] = shaped_i_ * in_phase;
      aux[i] = shaped_q_ * quadrature;
    } else {
      out[i] = shaped_i_ * in_phase + shaped_q_ * quadrature;

      // The symbol staircase. It sits at +1.0 for the whole preamble, so it
      // has to be DC-blocked or it parks the LPG open; the blocker is why the
      // registered aux gain is negative.
      const float staircase = (shaped_i_ + shaped_q_) * \
          (1.0f / (2.0f * kDigitalModulationRadius));
      dc_aux_out_ = staircase - dc_aux_in_ + \
          kDigitalModulationDcPole * dc_aux_out_;
      dc_aux_in_ = staircase;
      aux[i] = dc_aux_out_;
    }
  }
}

}  // namespace plaits
