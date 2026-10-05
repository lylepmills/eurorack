// Guards the scale bank above seven degrees: Chromatic (12) and Diminished (8)
// through the degree math and all four scale engines. Built by
// scale_voices_wide_test.sh with a recipe-style PLAITS_SCALE_BANK override, so
// the ordinary eight-scale fallback in build_config.h is not what runs here.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/engine2/diatonic_chord_engine.h"
#include "plaits/dsp/engine2/scale_stack_engine.h"
#include "plaits/dsp/engine2/scale_voices.h"
#include "plaits/dsp/engine2/wavetable_chord_engine.h"
#include "plaits/dsp/engine2/wavetable_scale_stack_engine.h"

using namespace plaits;
using namespace stmlib;

namespace {

const size_t kAudioBlockSize = 24;  // As in plaits_test.cc and the firmware.
char ram_block[16384];

void Fail(const char* message, int a, int b, float value) {
  fprintf(stderr, "FAIL: %s (%d, %d, %f)\n", message, a, b, value);
  exit(1);
}

void CheckDegreeMath() {
  for (int scale = 0; scale < kScaleVoicesNumScales; ++scale) {
    const Scale& s = kScaleVoicesScales[scale];
    if (s.num_degrees < 2 || s.num_degrees > kScaleVoicesMaxDegrees) {
      Fail("degree count", scale, s.num_degrees, 0.0f);
    }
    // Every degree in a five-octave window maps to its own pitch, the octave
    // wraps exactly, and quantizing a degree's pitch returns that degree.
    for (int degree = -2 * s.num_degrees; degree < 3 * s.num_degrees; ++degree) {
      const float note = ScaleDegreeToNote(degree, scale);
      const float next = ScaleDegreeToNote(degree + 1, scale);
      if (!(next > note)) Fail("degrees not ascending", scale, degree, note);
      const float octave_up = ScaleDegreeToNote(degree + s.num_degrees, scale);
      if (fabsf(octave_up - note - 12.0f) > 1e-5f) {
        Fail("octave wrap", scale, degree, octave_up - note);
      }
      float residual = 1.0f;
      const int quantized = QuantizeToScale(48.0f + note, scale, &residual);
      const int expected = degree + 4 * s.num_degrees;
      if (quantized != expected || fabsf(residual) > 1e-5f) {
        Fail("quantize round trip", scale, degree, residual);
      }
    }
  }
  // Chromatic quantizes every semitone to itself.
  for (int semitone = 0; semitone < 36; ++semitone) {
    float residual = 1.0f;
    const int degree = QuantizeToScale(36.0f + semitone, 0, &residual);
    if (degree != 36 + semitone || fabsf(residual) > 1e-5f) {
      Fail("chromatic semitone", semitone, degree, residual);
    }
  }
}

template<typename T>
void CheckEngine(const char* name) {
  BufferAllocator allocator(ram_block, sizeof(ram_block));
  static T engine;
  engine.Init(&allocator);
  engine.LoadUserData(NULL);
  const float values[] = { 0.0f, 0.5f, 1.0f };
  for (int scale = 0; scale < kScaleVoicesNumScales; ++scale) {
    for (float harmonics : values) {
      for (float timbre : values) {
        for (float morph : values) {
          engine.Reset();
          EngineParameters p = {};
          p.note = 48.0f;
          p.accent = 1.0f;
          p.harmonics = harmonics;
          p.timbre = timbre;
          p.morph = morph;
          p.macro = (scale + 0.5f) / kScaleVoicesNumScales;
          float energy = 0.0f;
          for (int block = 0; block < 64; ++block) {
            float out[kAudioBlockSize];
            float aux[kAudioBlockSize];
            p.trigger = block == 0
                ? TRIGGER_RISING_EDGE | TRIGGER_HIGH
                : TRIGGER_UNPATCHED;
            bool already_enveloped;
            engine.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
            for (size_t i = 0; i < kAudioBlockSize; ++i) {
              if (!std::isfinite(out[i]) || !std::isfinite(aux[i]) ||
                  fabsf(out[i]) > 4.0f || fabsf(aux[i]) > 4.0f) {
                fprintf(stderr, "%s: ", name);
                Fail("non-finite or loud sample", scale, block, out[i]);
              }
              energy += out[i] * out[i];
            }
          }
          if (energy < 1e-3f) {
            fprintf(stderr, "%s h=%.1f t=%.1f m=%.1f: ", name, harmonics,
                    timbre, morph);
            Fail("silent", scale, 0, energy);
          }
        }
      }
    }
  }
  printf("ok %s\n", name);
}

}  // namespace

int main() {
  CheckDegreeMath();
  printf("ok degree math (%d scales)\n", kScaleVoicesNumScales);
  CheckEngine<DiatonicChordEngine>("Diatonic Chord");
  CheckEngine<ScaleStackEngine>("Scale Stack");
  CheckEngine<WavetableChordEngine>("Wavetable Chord");
  CheckEngine<WavetableScaleStackEngine>("Wavetable Scale Stack");
  return 0;
}
