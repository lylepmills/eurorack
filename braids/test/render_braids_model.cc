// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Reference renderer for the Braids → Plaits Palette port.
//
// Renders any Braids MacroOscillator shape to a WAV so a ported engine can be
// compared against the model it comes from. Wave 1 of the port did this in a
// throwaway scratchpad, so every A/B number in BRAIDS_PORT_PROGRESS.md rests
// on a harness nobody can rerun. This is that harness, committed.
//
// Two properties matter for the comparison to mean anything:
//
//   * Braids runs at 96 kHz (braids.cc:141). A port running at 48 kHz must be
//     compared against a DECIMATED reference, not the native one, or every
//     difference above 24 kHz is charged to the port. `--rate 48000` applies a
//     127-tap windowed-sinc halfband before dropping every other sample.
//   * Braids renders in blocks of 24 (kBlockSize, braids.cc:57) and several
//     render functions are 2x-unrolled loops that assume an even size. The
//     block size here is fixed at 24 for that reason.
//
// Every shape is reachable by name, including QUESTION_MARK, which the module
// itself gates behind the marquee easter egg (ui.h:89).
//
// Usage:
//   render_braids_model --shape CSAW --note 45 --seconds 4 \
//       --p1 0.0:1.0 --p2 0.5 --rate 48000 --out csaw-ref.wav

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "braids/macro_oscillator.h"

using namespace braids;

namespace {

const size_t kBlockSize = 24;
const uint32_t kBraidsSampleRate = 96000;

// Order is settings.h's MacroOscillatorShape, QUESTION_MARK included. The
// module's own display strings are unprintable glyphs, so these are the enum
// identifiers.
const char* const kShapeNames[] = {
    "CSAW", "MORPH", "SAW_SQUARE", "SINE_TRIANGLE", "BUZZ",
    "SQUARE_SUB", "SAW_SUB", "SQUARE_SYNC", "SAW_SYNC",
    "TRIPLE_SAW", "TRIPLE_SQUARE", "TRIPLE_TRIANGLE", "TRIPLE_SINE",
    "TRIPLE_RING_MOD", "SAW_SWARM", "SAW_COMB", "TOY",
    "DIGITAL_FILTER_LP", "DIGITAL_FILTER_PK", "DIGITAL_FILTER_BP",
    "DIGITAL_FILTER_HP",
    "VOSIM", "VOWEL", "VOWEL_FOF", "HARMONICS",
    "FM", "FEEDBACK_FM", "CHAOTIC_FEEDBACK_FM",
    "PLUCKED", "BOWED", "BLOWN", "FLUTED",
    "STRUCK_BELL", "STRUCK_DRUM", "KICK", "CYMBAL", "SNARE",
    "WAVETABLES", "WAVE_MAP", "WAVE_LINE", "WAVE_PARAPHONIC",
    "FILTERED_NOISE", "TWIN_PEAKS_NOISE", "CLOCKED_NOISE",
    "GRANULAR_CLOUD", "PARTICLE_NOISE",
    "DIGITAL_MODULATION", "QUESTION_MARK"};

const size_t kNumShapes = sizeof(kShapeNames) / sizeof(kShapeNames[0]);

int ShapeFromName(const std::string& name) {
  for (size_t i = 0; i < kNumShapes; ++i) {
    if (name == kShapeNames[i]) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// "0.25" is a constant, "0.0:1.0" a linear sweep across the whole render.
struct Sweep {
  float start;
  float end;

  float At(float t) const { return start + (end - start) * t; }
};

bool ParseSweep(const std::string& text, Sweep* sweep) {
  const size_t colon = text.find(':');
  char* endptr = NULL;
  if (colon == std::string::npos) {
    const std::string trimmed = text;
    sweep->start = strtof(trimmed.c_str(), &endptr);
    if (endptr == trimmed.c_str()) {
      return false;
    }
    sweep->end = sweep->start;
    return true;
  }
  const std::string lhs = text.substr(0, colon);
  const std::string rhs = text.substr(colon + 1);
  sweep->start = strtof(lhs.c_str(), &endptr);
  if (endptr == lhs.c_str()) {
    return false;
  }
  sweep->end = strtof(rhs.c_str(), &endptr);
  return endptr != rhs.c_str();
}

int16_t ToParameter(float normalized) {
  float v = normalized * 32767.0f;
  if (v < 0.0f) {
    v = 0.0f;
  } else if (v > 32767.0f) {
    v = 32767.0f;
  }
  return static_cast<int16_t>(v);
}

// 127-tap windowed-sinc at fc = 0.25 (of 96 kHz), Blackman-Harris. Stopband is
// below -90 dB, so the decimated reference carries no image energy worth
// charging to a port.
void Decimate2x(const std::vector<float>& in, std::vector<float>* out) {
  const int kTaps = 127;
  const int kHalf = kTaps / 2;
  std::vector<float> h(kTaps);
  double sum = 0.0;
  for (int i = 0; i < kTaps; ++i) {
    const int n = i - kHalf;
    const double sinc = (n == 0) ? 0.5 : sin(M_PI * 0.5 * n) / (M_PI * n);
    const double x = 2.0 * M_PI * i / (kTaps - 1);
    const double w = 0.35875 - 0.48829 * cos(x) + 0.14128 * cos(2.0 * x) -
        0.01168 * cos(3.0 * x);
    h[i] = static_cast<float>(sinc * w);
    sum += h[i];
  }
  for (int i = 0; i < kTaps; ++i) {
    h[i] = static_cast<float>(h[i] / sum);
  }

  out->clear();
  out->reserve(in.size() / 2);
  const int n = static_cast<int>(in.size());
  for (int i = 0; i < n; i += 2) {
    float acc = 0.0f;
    for (int k = 0; k < kTaps; ++k) {
      const int j = i + k - kHalf;
      if (j >= 0 && j < n) {
        acc += h[k] * in[j];
      }
    }
    out->push_back(acc);
  }
}

void WriteU32(FILE* fp, uint32_t v) { fwrite(&v, 4, 1, fp); }
void WriteU16(FILE* fp, uint16_t v) { fwrite(&v, 2, 1, fp); }

bool WriteWav(const char* path,
              const std::vector<float>& samples,
              uint32_t sample_rate) {
  FILE* fp = fopen(path, "wb");
  if (!fp) {
    return false;
  }
  const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * 2);
  fwrite("RIFF", 4, 1, fp);
  WriteU32(fp, 36 + data_bytes);
  fwrite("WAVE", 4, 1, fp);
  fwrite("fmt ", 4, 1, fp);
  WriteU32(fp, 16);
  WriteU16(fp, 1);
  WriteU16(fp, 1);
  WriteU32(fp, sample_rate);
  WriteU32(fp, sample_rate * 2);
  WriteU16(fp, 2);
  WriteU16(fp, 16);
  fwrite("data", 4, 1, fp);
  WriteU32(fp, data_bytes);
  for (size_t i = 0; i < samples.size(); ++i) {
    float v = samples[i];
    if (v > 32767.0f) {
      v = 32767.0f;
    } else if (v < -32768.0f) {
      v = -32768.0f;
    }
    const int16_t s = static_cast<int16_t>(v);
    fwrite(&s, 2, 1, fp);
  }
  fclose(fp);
  return true;
}

void PrintUsage() {
  fprintf(stderr,
          "usage: render_braids_model --shape NAME --out FILE [options]\n"
          "\n"
          "  --shape NAME       one of the shapes listed by --list\n"
          "  --out FILE         destination WAV\n"
          "  --note N           MIDI note, may be fractional (default 48)\n"
          "  --seconds S        render length (default 3)\n"
          "  --p1 V | A:B       TIMBRE, 0..1, constant or swept (default 0.5)\n"
          "  --p2 V | A:B       COLOR, 0..1, constant or swept (default 0.5)\n"
          "  --trigger-hz H     strike rate; 0 strikes once at t=0 (default 0)\n"
          "  --rate R           96000 (native) or 48000 (decimated)\n"
          "  --list             print every shape name and exit\n");
}

}  // namespace

// Static storage duration, matching braids.cc:59's own `MacroOscillator osc;`
// at file scope. A stack-local `osc` in main() left DigitalOscillator's
// delay_lines_ union (digital_oscillator.h -- NOT part of the state_ union
// Init() memsets) as uninitialized stack garbage on every model whose first
// strike depends on a zeroed delay line, which on the real module's global
// object is zero-initialized for free. Verified against PLUCKED: a
// stack-local osc rendered a garbage-driven near-full-scale signal that
// never decayed at ANY Damping/TIMBRE setting (flat within ~1 dB over 15 s,
// note 45 and note 96 both), while this static osc decays exactly as the
// TIMBRE/loss formula predicts (silent by ~800 ms at TIMBRE 0, a slow
// multi-second decay at TIMBRE 0.5) -- see the plucked engine's port notes.
static MacroOscillator osc;

int main(int argc, char** argv) {
  std::string shape_name;
  std::string out_path;
  float note = 48.0f;
  float seconds = 3.0f;
  float trigger_hz = 0.0f;
  uint32_t rate = kBraidsSampleRate;
  Sweep p1 = {0.5f, 0.5f};
  Sweep p2 = {0.5f, 0.5f};

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--list") {
      for (size_t s = 0; s < kNumShapes; ++s) {
        printf("%2zu  %s\n", s, kShapeNames[s]);
      }
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    }
    if (i + 1 >= argc) {
      fprintf(stderr, "error: %s needs a value\n", arg.c_str());
      return 1;
    }
    const std::string value = argv[++i];
    if (arg == "--shape") {
      shape_name = value;
    } else if (arg == "--out") {
      out_path = value;
    } else if (arg == "--note") {
      note = strtof(value.c_str(), NULL);
    } else if (arg == "--seconds") {
      seconds = strtof(value.c_str(), NULL);
    } else if (arg == "--trigger-hz") {
      trigger_hz = strtof(value.c_str(), NULL);
    } else if (arg == "--rate") {
      rate = static_cast<uint32_t>(strtoul(value.c_str(), NULL, 10));
    } else if (arg == "--p1") {
      if (!ParseSweep(value, &p1)) {
        fprintf(stderr, "error: bad --p1 '%s'\n", value.c_str());
        return 1;
      }
    } else if (arg == "--p2") {
      if (!ParseSweep(value, &p2)) {
        fprintf(stderr, "error: bad --p2 '%s'\n", value.c_str());
        return 1;
      }
    } else {
      fprintf(stderr, "error: unknown option %s\n", arg.c_str());
      PrintUsage();
      return 1;
    }
  }

  if (shape_name.empty() || out_path.empty()) {
    PrintUsage();
    return 1;
  }
  const int shape = ShapeFromName(shape_name);
  if (shape < 0) {
    fprintf(stderr, "error: unknown shape '%s' (try --list)\n",
            shape_name.c_str());
    return 1;
  }
  if (rate != kBraidsSampleRate && rate != kBraidsSampleRate / 2) {
    fprintf(stderr, "error: --rate must be 96000 or 48000\n");
    return 1;
  }

  osc.Init();
  osc.set_shape(static_cast<MacroOscillatorShape>(shape));
  // Braids' pitch is 14-bit with 128 counts per semitone (braids.cc:258).
  osc.set_pitch(static_cast<int16_t>(note * 128.0f));
  osc.Strike();

  const size_t total = static_cast<size_t>(seconds * kBraidsSampleRate);
  const size_t blocks = total / kBlockSize;
  const float trigger_period = trigger_hz > 0.0f
      ? kBraidsSampleRate / trigger_hz
      : 0.0f;
  float next_trigger = trigger_period;

  std::vector<float> native;
  native.reserve(blocks * kBlockSize);

  int16_t buffer[kBlockSize];
  uint8_t sync_buffer[kBlockSize];
  memset(sync_buffer, 0, sizeof(sync_buffer));

  for (size_t b = 0; b < blocks; ++b) {
    const float t = blocks > 1 ? static_cast<float>(b) / (blocks - 1) : 0.0f;
    osc.set_parameters(ToParameter(p1.At(t)), ToParameter(p2.At(t)));

    const float block_start = static_cast<float>(b * kBlockSize);
    if (trigger_period > 0.0f && block_start >= next_trigger) {
      osc.Strike();
      next_trigger += trigger_period;
    }

    osc.Render(sync_buffer, buffer, kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
      native.push_back(static_cast<float>(buffer[i]));
    }
  }

  if (rate == kBraidsSampleRate) {
    if (!WriteWav(out_path.c_str(), native, rate)) {
      fprintf(stderr, "error: cannot write %s\n", out_path.c_str());
      return 1;
    }
  } else {
    std::vector<float> decimated;
    Decimate2x(native, &decimated);
    if (!WriteWav(out_path.c_str(), decimated, rate)) {
      fprintf(stderr, "error: cannot write %s\n", out_path.c_str());
      return 1;
    }
  }

  printf("%s  shape=%s note=%.2f p1=%.3f:%.3f p2=%.3f:%.3f rate=%u\n",
         out_path.c_str(), kShapeNames[shape], note,
         p1.start, p1.end, p2.start, p2.end, rate);
  return 0;
}
