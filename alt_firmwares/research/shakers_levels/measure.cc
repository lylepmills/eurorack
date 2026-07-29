// Shaker level measurement: what each of the sixteen instruments actually
// puts out, swept across the space a player moves through.
//
// The engine's per-preset `makeup` gains exist because STK's resonators are
// un-normalised and Cook assumed a downstream master fader per instrument.
// Under ONE knob they have to match each other instead, and the only way to
// know whether they do is to measure every instrument over the same grid.
//
// Reports, per instrument: mean RMS in dB relative to the sixteen-instrument
// mean, the worst-case peak (which is what limits how far a sparse instrument
// can be lifted before it clips), and the makeup gain that would centre it.
//
// Build and run (host only, no toolchain beyond the compiler):
//   cd alt_firmwares/research/shakers_levels && ./run.sh
//
// SPDX-License-Identifier: MIT

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "stmlib/utils/buffer_allocator.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/engine2/shakers_engine.h"

using namespace plaits;
using namespace stmlib;

namespace {

const int kBlock = 24;
// Long enough that the sparse instruments (water drops, coins in a mug) get a
// representative number of events rather than whatever happens to fall in a
// short window -- undersampling them is what makes a sparse sound read as a
// quiet one.
const float kSeconds = 8.0f;
const int kNumInstruments = 16;

const char* kNames[kNumInstruments] = {
  "Maraca", "Cabasa", "Sekere", "Tambourine",
  "Sleigh bells", "Bamboo chimes", "Angklung", "Coke can",
  "Sticks", "Crunch", "Big rocks", "Little rocks",
  "Coins in a mug", "Water drops", "Guiro", "Wrench",
};

struct Stat {
  double sum_sq = 0.0;
  double frames = 0.0;
  float peak = 0.0f;
  void Add(float v) {
    sum_sq += double(v) * double(v);
    frames += 1.0;
    const float a = fabsf(v);
    if (a > peak) peak = a;
  }
  float Rms() const { return frames > 0.0 ? float(sqrt(sum_sq / frames)) : 0.0f; }
};

char engine_ram[16 * 1024];

// One (instrument, timbre, morph, macro) point, rendered from a fresh engine so
// no state carries between points.
Stat Measure(int instrument, float timbre, float morph, float macro, float note) {
  BufferAllocator allocator(engine_ram, sizeof(engine_ram));
  ShakersEngine engine;
  engine.Init(&allocator);
  engine.LoadUserData(NULL);
  engine.Reset();

  EngineParameters p;
  p.note = note;
  p.accent = 0.8f;
  p.chord_set_option = 0;
  // Instrument selection is the low edge of the preset's slice, so a preset is
  // measured where the knob actually lands it rather than at a boundary.
  p.harmonics = (float(instrument) + 0.5f) / float(kNumInstruments);
  p.timbre = timbre;
  p.morph = morph;
  p.macro = macro;
  // Unpatched: the drone case, which is what the selector is swept in.
  p.trigger = TRIGGER_UNPATCHED;

  const int blocks = int(kSeconds * 48000.0f / kBlock);
  // Discard the first half second: the energy model ramps from zero, and a
  // preset with a slow system decay would otherwise be scored on its onset.
  const int warmup = int(0.5f * 48000.0f / kBlock);

  float out[kBlock];
  float aux[kBlock];
  Stat s;
  for (int b = 0; b < blocks; ++b) {
    bool already = false;
    engine.Render(p, out, aux, kBlock, &already);
    if (b < warmup) continue;
    for (int i = 0; i < kBlock; ++i) s.Add(out[i]);
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  // The grid: TIMBRE is shake strength, MORPH is decay, MACRO is object count.
  // Deliberately excludes the extremes -- an instrument shaken at 0.0 is silent
  // for every preset, which would flatter the match without meaning anything.
  const float kTimbre[] = { 0.35f, 0.6f, 0.85f };
  const float kMorph[] = { 0.3f, 0.5f, 0.7f };
  const float kMacro[] = { 0.35f, 0.5f, 0.65f };
  const float kNotes[] = { 48.0f, 60.0f };

  const bool verbose = argc > 1 && strcmp(argv[1], "--verbose") == 0;

  std::vector<float> mean_rms(kNumInstruments, 0.0f);
  std::vector<float> worst_peak(kNumInstruments, 0.0f);

  for (int inst = 0; inst < kNumInstruments; ++inst) {
    double acc = 0.0;
    int points = 0;
    float peak = 0.0f;
    for (float t : kTimbre) {
      for (float m : kMorph) {
        for (float o : kMacro) {
          for (float n : kNotes) {
            Stat s = Measure(inst, t, m, o, n);
            // Mean of the per-point RMS, not RMS-of-everything: each knob
            // position should count once, otherwise the loudest corner of the
            // space dominates the instrument's score.
            acc += s.Rms();
            ++points;
            if (s.peak > peak) peak = s.peak;
            if (verbose) {
              printf("    %-15s t=%.2f m=%.2f o=%.2f n=%.0f  rms %.5f peak %.4f\n",
                     kNames[inst], t, m, o, n, s.Rms(), s.peak);
            }
          }
        }
      }
    }
    mean_rms[inst] = float(acc / points);
    worst_peak[inst] = peak;
  }

  double total = 0.0;
  for (int i = 0; i < kNumInstruments; ++i) total += mean_rms[i];
  const float target = float(total / kNumInstruments);

  printf("\n%-16s %10s %8s %8s %8s %9s\n",
         "instrument", "rms", "dB", "peak", "crest", "want x");
  float worst = 0.0f;
  for (int i = 0; i < kNumInstruments; ++i) {
    const float db = 20.0f * log10f(mean_rms[i] / target);
    const float crest = mean_rms[i] > 0.0f ? worst_peak[i] / mean_rms[i] : 0.0f;
    const float want = mean_rms[i] > 0.0f ? target / mean_rms[i] : 0.0f;
    // The ceiling a lift runs into: peak must stay under 1.0, so an instrument
    // with a high crest factor cannot be raised as far as its RMS suggests.
    const float headroom = worst_peak[i] > 0.0f ? 1.0f / worst_peak[i] : 0.0f;
    printf("%-16s %10.5f %+8.2f %8.4f %8.1f %9.3f%s\n",
           kNames[i], mean_rms[i], db, worst_peak[i], crest, want,
           want > headroom ? "  <-- clips before it levels" : "");
    if (fabsf(db) > worst) worst = fabsf(db);
  }
  printf("\nworst deviation %.2f dB\n", worst);
  return 0;
}
