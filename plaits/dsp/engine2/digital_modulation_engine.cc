// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' QPSK model: a carrier driven by a framed packet of dibits.

#include "plaits/dsp/engine2/digital_modulation_engine.h"

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

// Braids' wav_sine is -cos, a quarter period behind lut_sine.
inline float BraidsSine(float phase) {
  return Sine(phase + 0.75f);
}

}  // namespace

void DigitalModulationEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void DigitalModulationEngine::Reset() {
  phase_ = 0.0f;
  symbol_phase_ = 0.0f;
  payload_filter_ = kDigitalModulationStockPayload;
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
    // Braids' strike resets the symbol count only: the packet restarts from
    // its preamble while the carrier runs on.
    symbol_count_ = 0;
    data_byte_ = 0;
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

  // MACRO sweeps the payload byte around Braids' mid value.
  const float payload = ApplyMacro(
      kDigitalModulationStockPayload, 0.0f, 255.0f, parameters.macro);

  // MORPH shapes the symbol transitions. At zero the constellation points are
  // hard steps, which is Braids; above it they glide.
  const float shaping = parameters.morph;
  const float shape_pole = shaping * shaping * 0.9995f;

  const bool stereo = PLAITS_STEREO_DIGITAL_MODULATION && parameters.stereo;

  for (size_t i = 0; i < size; ++i) {
    phase_ += carrier_increment;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
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
          payload_filter_ += (payload - payload_filter_) * \
              (1.0f - kDigitalModulationPayloadPole);
          data_byte_ = static_cast<int>(payload_filter_) & 0xff;
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
