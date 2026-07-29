// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// MEASUREMENT PROBE for BRAIDS_PORT_SPEC.md section 3.11 -- the `fluted` gate.
// This is not engine code. It exists to answer one question: across the MORPH
// range, does the dominant partial track the played note?
//
// Two models:
//   port    -- the proposed 48 kHz float port, with MORPH as the reflection
//              filter corner in harmonics of the note (2..40), the computed
//              ReedPipe filter_delay, and the corrected DC blockers.
//   braids  -- the real fixed-point DigitalOscillator::RenderFluted at 96 kHz,
//              decimated to 48 kHz. Ground truth for the topology.
//
// Emits raw float32 to a file; analysis lives in fluted_probe.py.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "braids/resources.h"
// Drive Braids through MacroOscillator, NOT DigitalOscillator directly:
// `fn_table_` is indexed by (MacroOscillatorShape - MACRO_OSC_SHAPE_TRIPLE_
// RING_MOD), and the DigitalOscillatorShape enum in the header is in a
// DIFFERENT order. set_shape(OSC_SHAPE_FLUTED) silently renders Snare.
#include "braids/macro_oscillator.h"
#include "braids/settings.h"

namespace {

// ---------------------------------------------------------------------------
// Shared pieces of Braids' FLUT, expressed in floats normalised so that the
// int16 full scale of 32768 is 1.0.

// lut_blowing_jet spans x = 0..2 over 257 entries: index = floor(x * 128),
// clamped to Braids' own 0..65535 pre-shift clamp. Braids does not interpolate.
inline float JetTable(float x) {
  int index = static_cast<int>(x * 32768.0f);
  if (index < 0) index = 0;
  if (index > 65535) index = 65535;
  return static_cast<float>(braids::lut_blowing_jet[index >> 8]) *
      (1.0f / 32768.0f);
}

// lut_blowing_envelope, read with Braids' `<< 1` and its terminal clamp.
inline float BlowingEnvelope(int index) {
  const int kMax = LUT_BLOWING_ENVELOPE_SIZE - 32;
  if (index > kMax) index = kMax;
  if (index < 0) index = 0;
  return static_cast<float>(braids::lut_blowing_envelope[index]) *
      (2.0f / 32768.0f);
}

// Braids stores every waveguide sample as `value >> 9` into an int8, so an
// int8 of v reads back as v * 512. The shift FLOORS (R8); the saturation is
// this port's deviation, because a wrap inside a feedback loop can send the
// pipe off on its own.
inline int8_t QuantizeToInt8(float value) {
  const float scaled = value * 64.0f;
  int quantized = static_cast<int>(scaled);
  if (scaled < static_cast<float>(quantized)) --quantized;
  if (quantized < -128) quantized = -128;
  if (quantized > 127) quantized = 127;
  return static_cast<int8_t>(quantized);
}

uint32_t rng_state = 0x12345678u;
inline float Noise() {
  rng_state = rng_state * 1664525u + 1013904223u;
  return static_cast<float>(rng_state >> 9) * (1.0f / 8388608.0f) - 1.0f;
}

// Plaits' pitch anchor, at the port's rate.
const float kCorrectedSampleRate = 47872.34f;
inline float NoteToFrequency(float midi_note) {
  midi_note -= 9.0f;
  if (midi_note < -128.0f) midi_note = -128.0f;
  if (midi_note > 127.0f) midi_note = 127.0f;
  return ((440.0f / 8.0f) / kCorrectedSampleRate) * 0.25f *
      powf(2.0f, midi_note / 12.0f);
}

inline float ApplyMacro(float stock, float minimum, float maximum, float macro) {
  const float amount = macro * 2.0f;
  return macro < 0.5f
      ? minimum + (stock - minimum) * amount
      : stock + (maximum - stock) * (amount - 1.0f);
}

// ---------------------------------------------------------------------------
// The proposed port. 48 kHz, one loop iteration per output sample. Delay lines
// are half Braids' length, which reproduces its 58.1 / 38.2 Hz floors exactly.

const int kPortBoreLength = 2048;
const int kPortJetLength = 512;

struct PortFluted {
  int8_t bore[kPortBoreLength];
  int8_t jet[kPortJetLength];
  uint32_t ptr;
  float lp;             // reflection one-pole
  float dc_x, dc_y;     // in-loop DC blocker
  float out_dc_x, out_dc_y;
  int excitation;

  // Knobs of the model, so the sweep can vary them independently.
  int drive_mode;       // 0 = scale the jet table input, 1 = scale its output
  float in_loop_dc_pole;
  float out_dc_pole;
  float pitch_term;     // <0 selects the computed ReedPipe filter_delay

  void Reset() {
    memset(bore, 0, sizeof(bore));
    memset(jet, 0, sizeof(jet));
    ptr = 0;
    lp = dc_x = dc_y = out_dc_x = out_dc_y = 0.0f;
    excitation = 0;
  }

  float ReadLine(const int8_t* line, int length, float delay) {
    const uint32_t integral = static_cast<uint32_t>(delay);
    const float fractional = delay - static_cast<float>(integral);
    const uint32_t read = ptr + 2u * static_cast<uint32_t>(length) - integral;
    const float a = static_cast<float>(line[read & (length - 1)]);
    const float b = static_cast<float>(line[(read - 1) & (length - 1)]);
    return (a + (b - a) * fractional) * (1.0f / 64.0f);
  }

  void Render(float note, float harmonics, float timbre, float morph,
              float macro, float* out, int size) {
    const float frequency = NoteToFrequency(note);

    // MORPH: the reflection corner in harmonics of the note, ReedPipe's idiom.
    const float brightness_harmonic = 2.0f + 38.0f * morph;
    float b = 6.2832f * brightness_harmonic * frequency;
    if (b < 0.02f) b = 0.02f;
    if (b > 0.90f) b = 0.90f;

    // Spec fix 1: the computed group-delay compensation, not a transplanted
    // constant. pitch_term >= 0 overrides it for the A/B.
    const float filter_delay = pitch_term >= 0.0f
        ? pitch_term
        : 0.88f * (1.0f - b) / b;
    float total_delay = 2.0f / frequency - filter_delay;

    // HARMONICS: Braids' integer jet fraction, 48/256 .. 79/256.
    const int jet_steps = 48 +
        (static_cast<int>(harmonics * 32767.0f) >> 10);
    float jet_delay = total_delay * static_cast<float>(jet_steps) *
        (1.0f / 256.0f);
    float bore_delay = total_delay - jet_delay;

    // Braids' octave fold, at the port's line lengths.
    int guard = 0;
    while (guard < 16 &&
           (bore_delay > static_cast<float>(kPortBoreLength - 1) ||
            jet_delay > static_cast<float>(kPortJetLength - 1))) {
      bore_delay *= 0.5f;
      jet_delay *= 0.5f;
      ++guard;
    }
    if (bore_delay < 2.0f) bore_delay = 2.0f;
    if (jet_delay < 2.0f) jet_delay = 2.0f;

    // TIMBRE: Braids' breath_intensity = 2100 - (p0 >> 4), so the noise depth
    // is breath_intensity / 4096.
    const int breath_intensity = 2100 -
        (static_cast<int>(timbre * 32767.0f) >> 4);
    const float noise_depth = static_cast<float>(breath_intensity) *
        (1.0f / 4096.0f);

    const float drive = ApplyMacro(1.0f, 0.5f, 2.0f, macro);

    for (int i = 0; i < size; ++i) {
      const float bore_value = ReadLine(bore, kPortBoreLength, bore_delay);
      const float jet_value = ReadLine(jet, kPortJetLength, jet_delay);

      float breath = BlowingEnvelope(excitation);
      breath += Noise() * noise_depth * breath;

      // Reflection: a one-pole toward the INVERTED bore signal.
      lp += b * (-bore_value - lp);
      float reflection = lp;
      dc_y = in_loop_dc_pole * dc_y + reflection - dc_x;
      dc_x = reflection;
      reflection = dc_y;

      jet[ptr & (kPortJetLength - 1)] =
          QuantizeToInt8(breath - 0.5f * reflection);

      const float jet_in = drive_mode == 0 ? jet_value * drive : jet_value;
      float jet_out = JetTable(jet_in);
      if (drive_mode == 1) jet_out *= drive;
      bore[ptr & (kPortBoreLength - 1)] =
          QuantizeToInt8(jet_out + 0.5f * reflection);
      ++ptr;

      const float pressure = 0.5f * bore_value;
      out_dc_y = out_dc_pole * out_dc_y + pressure - out_dc_x;
      out_dc_x = pressure;
      out[i] = out_dc_y;

      // Braids advances the envelope on 3 of every 4 iterations at 96 kHz;
      // at 48 kHz that is 3 of every 2 output samples.
      excitation += (i & 1) ? 2 : 1;
    }
  }
};

// ---------------------------------------------------------------------------

void Usage() {
  fprintf(stderr,
      "fluted_probe --model port|braids --note N --harmonics X --timbre X "
      "--morph X --macro X --seconds N --out PATH\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::string model = "port";
  std::string out_path;
  float note = 60.0f, harmonics = 0.5f, timbre = 0.5f, morph = 0.25f;
  float macro = 0.5f;
  float seconds = 3.0f;
  int drive_mode = 0;
  float in_loop_dc_pole = 0.98f;
  float out_dc_pole = 0.98f;
  float pitch_term = -1.0f;
  uint32_t seed = 0x12345678u;

  for (int i = 1; i < argc - 1; i += 2) {
    const std::string k = argv[i];
    const char* v = argv[i + 1];
    if (k == "--model") model = v;
    else if (k == "--out") out_path = v;
    else if (k == "--note") note = atof(v);
    else if (k == "--harmonics") harmonics = atof(v);
    else if (k == "--timbre") timbre = atof(v);
    else if (k == "--morph") morph = atof(v);
    else if (k == "--macro") macro = atof(v);
    else if (k == "--seconds") seconds = atof(v);
    else if (k == "--drive-mode") drive_mode = atoi(v);
    else if (k == "--loop-dc") in_loop_dc_pole = atof(v);
    else if (k == "--out-dc") out_dc_pole = atof(v);
    else if (k == "--pitch-term") pitch_term = atof(v);
    else if (k == "--seed") seed = strtoul(v, NULL, 0);
    else { Usage(); return 2; }
  }
  if (out_path.empty()) { Usage(); return 2; }

  rng_state = seed;
  const int total = static_cast<int>(seconds * 48000.0f);
  std::vector<float> buffer(total, 0.0f);

  if (model == "port") {
    static PortFluted engine;
    engine.Reset();
    engine.drive_mode = drive_mode;
    engine.in_loop_dc_pole = in_loop_dc_pole;
    engine.out_dc_pole = out_dc_pole;
    engine.pitch_term = pitch_term;
    const int kBlock = 24;
    for (int i = 0; i < total; i += kBlock) {
      const int n = (total - i) < kBlock ? (total - i) : kBlock;
      engine.Render(note, harmonics, timbre, morph, macro, &buffer[i], n);
    }
  } else if (model == "braids") {
    // Braids' own fixed-point FLUT at 96 kHz, decimated with the [0.25, 0.5,
    // 0.25] kernel the ports' A/B harness uses.
    static braids::MacroOscillator osc;
    osc.Init();
    osc.set_shape(braids::MACRO_OSC_SHAPE_FLUTED);
    // pitch is in 1/128 semitone units, MIDI 60 == 60 * 128.
    osc.set_pitch(static_cast<int16_t>(note * 128.0f));
    osc.set_parameters(
        static_cast<int16_t>(timbre * 32767.0f),
        static_cast<int16_t>(harmonics * 32767.0f));
    osc.Strike();
    const int kBlock = 24;
    std::vector<int16_t> hi(total * 2 + kBlock, 0);
    for (int i = 0; i < total * 2; i += kBlock) {
      osc.Render(NULL, &hi[i], kBlock);
    }
    for (int i = 0; i < total; ++i) {
      const int j = i * 2;
      const float a = j > 0 ? hi[j - 1] : 0.0f;
      const float b = hi[j];
      const float c = hi[j + 1];
      buffer[i] = (0.25f * a + 0.5f * b + 0.25f * c) * (1.0f / 32768.0f);
    }
  } else {
    Usage();
    return 2;
  }

  FILE* f = fopen(out_path.c_str(), "wb");
  if (!f) { perror("fopen"); return 1; }
  fwrite(buffer.data(), sizeof(float), buffer.size(), f);
  fclose(f);
  return 0;
}
