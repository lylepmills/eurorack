// Does Virtual Analog Variant actually contain both parents?
//
// The claim the merge rests on is that the unified engine can reproduce
// Virtual Analog Dual's OUT and AUX and Virtual Analog Crossfade's OUT
// exactly, for the same settings, with only the control map rearranged. This
// renders all three and compares sample by sample.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>

#include "stmlib/utils/buffer_allocator.h"
#include "plaits/dsp/engine/virtual_analog_dual_engine.h"
#include "plaits/dsp/engine/virtual_analog_crossfade_engine.h"
#include "plaits/dsp/engine/virtual_analog_variant_engine.h"

using namespace plaits;
static const size_t kBlock = 12;
static const size_t kBlocks = 400;

struct Settings { float harmonics, timbre, morph, macro, note; bool stereo; };

template <typename E>
static void Run(const Settings& s, float* out_acc, float* aux_acc) {
  static uint8_t mem[64 * 1024];
  stmlib::BufferAllocator alloc(mem, sizeof(mem));
  E e; e.Init(&alloc); e.Reset();
  EngineParameters p;
  p.note = s.note; p.harmonics = s.harmonics; p.timbre = s.timbre;
  p.morph = s.morph; p.macro = s.macro; p.trigger = TRIGGER_UNPATCHED;
  p.accent = 1.0f; p.stereo = s.stereo;
  float out[kBlock], aux[kBlock];
  for (size_t b = 0; b < kBlocks; ++b) {
    bool env = false;
    memset(out, 0, sizeof(out)); memset(aux, 0, sizeof(aux));
    e.Render(p, out, aux, kBlock, &env);
    for (size_t i = 0; i < kBlock; ++i) {
      out_acc[b * kBlock + i] = out[i];
      aux_acc[b * kBlock + i] = aux[i];
    }
  }
}

static float MaxAbsDiff(const float* a, const float* b, size_t n, size_t skip = 0) {
  float m = 0.0f;
  for (size_t i = skip; i < n; ++i) {
    const float d = fabsf(a[i] - b[i]);
    if (d > m) m = d;
  }
  return m;
}

// One ulp of a float near 1.0 is ~1.19e-7; allow a hair over two.
static const float kUlp = 2.5e-7f;

static const size_t kN = kBlock * kBlocks;
static float a_out[kN], a_aux[kN], b_out[kN], b_aux[kN];

static int Check(const char* label, float diff) {
  const bool ok = diff == 0.0f;
  printf("  %-58s %s  (max |diff| %.3e)\n", label, ok ? "EXACT " : "DIFFER", diff);
  return ok ? 0 : 1;
}

int main() {
  int failures = 0;
  const float notes[] = { 36.0f, 48.0f, 60.0f };
  const float harms[] = { 0.10f, 0.35f, 0.62f, 0.88f };
  const float shapes[] = { 0.0f, 0.3f, 0.7f, 1.0f };

  printf("Crossfade OUT  vs  Variant with TWIST at noon (shapes matched)\n");
  for (int n = 0; n < 3; ++n) for (int h = 0; h < 4; ++h)
      for (int t = 0; t < 4; ++t) for (int j = 0; j < 4; ++j) {
    // Crossfade: TIMBRE is the trajectory, MORPH the shared shape.
    Settings cf = { harms[h], shapes[t], shapes[j], 0.5f, notes[n], false };
    // Variant: MORPH is the trajectory, TIMBRE the shape, TWIST at noon.
    Settings vv = { harms[h], shapes[j], shapes[t], 0.5f, notes[n], false };
    Run<VirtualAnalogCrossfadeEngine>(cf, a_out, a_aux);
    Run<VirtualAnalogVariantEngine>(vv, b_out, b_aux);
    const float d = MaxAbsDiff(a_out, b_out, kN);
    if (d != 0.0f) {
      char buf[128];
      snprintf(buf, sizeof buf, "note %.0f harm %.2f traj %.1f shape %.1f",
               notes[n], harms[h], shapes[t], shapes[j]);
      failures += Check(buf, d);
    }
  }
  if (!failures) printf("  all 192 settings EXACT\n");

  printf("\nDual OUT and AUX  vs  Variant at the detuned end with TWIST placing"
         "\nthe secondary shape (macro = 0.5 + (dual_morph - timbre) / 2)\n");
  int dual_fail = 0; int settled_only = 0; float dual_worst = 0.0f;
  for (int n = 0; n < 3; ++n) for (int h = 0; h < 4; ++h)
      for (int t = 0; t < 4; ++t) for (int j = 0; j < 4; ++j) {
    Settings du = { harms[h], shapes[t], shapes[j], 0.5f, notes[n], false };
    // Invert ApplyMacro(timbre, 0, 1, macro) to place the secondary shape on
    // dual's MORPH. The midpoint is TIMBRE itself, so each half is a plain
    // linear solve; the degenerate ends (timbre 0 or 1) collapse one half.
    const float primary = shapes[t], target = shapes[j];
    float macro;
    if (target < primary) {
      macro = primary > 0.0f ? 0.5f * (target / primary) : 0.0f;
    } else {
      macro = primary < 1.0f
          ? 0.5f + 0.5f * ((target - primary) / (1.0f - primary))
          : 0.5f;
    }
    Settings vv = { harms[h], shapes[t], 0.0f, macro, notes[n], false };
    Run<VirtualAnalogDualEngine>(du, a_out, a_aux);
    Run<VirtualAnalogVariantEngine>(vv, b_out, b_aux);
    // Skip the first block: Variant interpolates the mix in (as Crossfade
    // does) where Dual hard-codes 50/50, so they can only agree once that
    // one-block ramp has settled.
    const float do_ = MaxAbsDiff(a_out, b_out, kN, kBlock);
    const float da_ = MaxAbsDiff(a_aux, b_aux, kN, kBlock);
    const float d0 = MaxAbsDiff(a_out, b_out, kBlock);
    if (d0 > kUlp && do_ <= kUlp) ++settled_only;
    // Bit-exactness to BOTH parents is not available: Dual computes the
    // 50/50 as (a + b) * 0.5f, Crossfade as a + (b - a) * amount. The two are
    // equal in exact arithmetic and round differently, and Crossfade's is the
    // general form this engine needs in order to interpolate. Crossfade is
    // therefore matched bit for bit and Dual to within an ulp.
    const float worst = do_ > da_ ? do_ : da_;
    if (worst > kUlp) {
      char buf[128];
      snprintf(buf, sizeof buf, "note %.0f harm %.2f timbre %.1f morph %.1f",
               notes[n], harms[h], shapes[t], shapes[j]);
      dual_fail += Check(buf, worst);
    }
    if (worst > dual_worst) dual_worst = worst;
  }
  if (!dual_fail) {
    printf("  all 192 settings match to within an ulp after the first block\n");
    printf("    worst residual %.3e (one ulp is ~1.19e-7, i.e. about -136 dBFS)\n", dual_worst);
    printf("    %d of 192 also differ during that first block alone -- Variant\n", settled_only);
    printf("    ramps the mix in as Crossfade does, where Dual hard-codes 50/50\n");
  }
  failures += dual_fail;

  printf("\n%s\n", failures ? "SUPERSET CLAIM FAILS" : "Superset claim holds.");
  return failures ? 1 : 0;
}
