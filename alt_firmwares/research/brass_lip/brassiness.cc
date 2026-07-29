// Does nonlinear propagation buy brightness, and what does it cost?
//
// The valve had to be made gentle so lip tension could SELECT a partial
// (finding 3), and a gentle valve generates few upper harmonics -- which is why
// the engine sounds musical but not brassy. A real instrument's brightness does
// not come from its valve either: it comes from the wave steepening as it runs
// down the bore, which is also why brass brightens as it is blown harder.
//
// Measures three things, because the first is worthless without the other two:
//   1. spectral centroid and high/low balance -- did it get brighter?
//   2. does brightness RISE WITH BREATH -- expressive, or a fixed EQ tilt that
//      a static filter would have given for free?
//   3. partial lock and tuning -- did it break what was hard to win?
//
// The control mapping is lifted verbatim from final.cc rather than reinvented.
// An earlier version of this file used model.h's bare defaults and measured a
// model that was not the shipped one at all -- it reported -1900 cents of
// tuning error at brassiness 0, where the engine actually holds ~6 cents.
// If brassiness 0 does not reproduce final.cc, the harness is wrong.
//
// Build: g++ -O2 -w brassiness.cc -o /tmp/x -lm && /tmp/x
//
// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include "model.h"

namespace {

const float FS = 48000.0f;

float F0(const std::vector<float>& x) {
  const int lo = int(FS / 1200.0f), hi = int(FS / 40.0f);
  std::vector<double> r(hi + 2, 0.0);
  for (int l = lo; l <= hi + 1 && l < (int)x.size(); ++l) {
    double s = 0.0;
    for (size_t i = 0; i + l < x.size(); ++i) s += double(x[i]) * x[i + l];
    r[l] = s;
  }
  int best = 0; double bv = -1e300;
  for (int l = lo; l <= hi; ++l) if (r[l] > bv) { bv = r[l]; best = l; }
  if (best <= lo) return -1.0f;
  const double den = r[best - 1] - 2.0 * r[best] + r[best + 1];
  double d = fabs(den) > 1e-12 ? 0.5 * (r[best - 1] - r[best + 1]) / den : 0.0;
  if (d < -0.5) d = -0.5;
  if (d > 0.5) d = 0.5;
  return FS / (float(best) + float(d));
}

float Mag(const std::vector<float>& x, float freq) {
  const float wv = 2.0f * float(M_PI) * freq / FS;
  const float c = 2.0f * cosf(wv);
  float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
  for (float v : x) { s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
  return sqrtf(fabsf(s1 * s1 + s2 * s2 - c * s1 * s2)) / float(x.size());
}

struct Out {
  float rms = 0.0f, centroid = 0.0f, hi_lo_db = -99.0f, ratio = 0.0f;
  int n = 0;
  bool sounded = false;
};

// final.cc's Render(), with brassiness and the tap added.
Out Render(float note, float h, float timbre, float morph, float macro,
           float brassiness, int tap) {
  const float f = 440.0f * powf(2.0f, (note - 69.0f) / 12.0f), fb = f * 0.5f;
  BrassParams p;
  p.reflect = 0.995f; p.lip_zeta = 0.04f; p.a0 = 0.60f;
  p.lip_k = 3.0f; p.zc = 0.15f; p.out_tap = tap;
  p.brassiness = brassiness;
  p.bore_len = FS / fb * (0.5f + macro);
  const float ab = FS / p.bore_len;
  const float cr = 4.0f + 22.0f * morph;
  p.damp = 2.0f * float(M_PI) * cr * f / FS;
  if (p.damp < 0.02f) p.damp = 0.02f;
  if (p.damp > 0.90f) p.damp = 0.90f;
  int top = 8; while (top > 2 && ab * top > 3000.0f) --top;
  const float pp = 2 + h * float(top - 2);
  int n = int(pp); if (n > top) n = top;
  const float t = pp - float(n);
  p.lip_freq = ab * float(n) * (0.905f + 0.020f * t);
  const float mouth = 0.18f + 0.50f * timbre;

  BrassModel m; m.Init(9000); m.Clear();
  std::vector<float> tail;
  double acc = 0.0; long c = 0;
  for (long i = 0; i < long(FS * 1.2f); ++i) {
    const float o = m.Tick(p, mouth);
    if (i > long(FS * 0.7f)) {
      acc += double(o) * o; ++c;
      if (tail.size() < 24000) tail.push_back(o);
    }
  }
  Out r;
  r.n = n;
  r.rms = float(sqrt(acc / c));
  r.sounded = r.rms > 1e-4f;
  if (!r.sounded) return r;
  const float f0 = F0(tail);
  if (f0 <= 0.0f) { r.sounded = false; return r; }
  r.ratio = f0 / f;

  double num = 0.0, den = 0.0, lo = 0.0, hi = 0.0;
  for (int k = 1; k <= 40; ++k) {
    const float fh = f0 * float(k);
    if (fh > 0.45f * FS) break;
    const float mag = Mag(tail, fh);
    num += double(mag) * fh; den += mag;
    if (k <= 4) lo += double(mag) * mag; else hi += double(mag) * mag;
  }
  r.centroid = den > 0.0 ? float(num / den) : 0.0f;
  r.hi_lo_db = (lo > 0.0 && hi > 0.0) ? 10.0f * log10f(float(hi / lo)) : -99.0f;
  return r;
}

const char* kTapName[] = { "bell", "flow (AUX)", "junction (OUT)", "bell+flow" };

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "all";
  const float kBrass[] = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };

  if (mode == "all" || mode == "bright") {
    // OUT is the junction (the mouthpiece tap the engine ships).
    for (int tap : {2, 1}) {
      printf("=== brightness on the %s tap, note 48, MACRO 0.5, MORPH 0.45 ===\n",
             kTapName[tap]);
      printf("%7s | %-32s | %-32s\n", "", "centroid (Hz) by TIMBRE",
             "5th-and-up vs below (dB)");
      printf("%7s |", "brassy");
      for (float t : {0.15f, 0.40f, 0.70f, 0.95f}) printf(" T=%4.2f ", t);
      printf(" |");
      for (float t : {0.15f, 0.40f, 0.70f, 0.95f}) printf(" T=%4.2f ", t);
      printf("\n--------+----------------------------------+"
             "---------------------------------\n");
      for (float b : kBrass) {
        printf("%7.1f |", b);
        std::vector<Out> row;
        for (float t : {0.15f, 0.40f, 0.70f, 0.95f}) {
          Out o = Render(48.0f, 0.0f, t, 0.45f, 0.5f, b, tap);
          row.push_back(o);
          if (o.sounded) printf(" %6.0f ", o.centroid); else printf(" %6s ", "silent");
        }
        printf(" |");
        for (const Out& o : row) {
          if (o.sounded) printf(" %6.1f ", o.hi_lo_db); else printf(" %6s ", "-");
        }
        if (row.front().sounded && row.back().sounded) {
          printf("  breath span %+.0f Hz", row.back().centroid - row.front().centroid);
        }
        printf("\n");
      }
      printf("\n");
    }
  }

  if (mode == "all" || mode == "lock") {
    printf("=== partial lock and tuning (the thing this must not break) ===\n");
    printf("HARMONICS staircase at note 48; expect the ratio to climb "
           "1, 1.5, 2, 2.5, 3, 3.5, 4\n\n");
    for (float b : kBrass) {
      printf("brassy %5.1f: ", b);
      for (float h = 0.0f; h <= 1.001f; h += 0.0834f) {
        Out o = Render(48.0f, h, 0.55f, 0.45f, 0.5f, b, 2);
        if (o.sounded) printf("%5.2f ", o.ratio); else printf("%5s ", "--");
      }
      printf("\n");
    }
    printf("\nTuning error against the partial the lip was placed on:\n");
    for (float b : kBrass) {
      double sum = 0.0, mx = 0.0; int c = 0, lost = 0;
      for (float note : {36.0f, 48.0f, 60.0f, 72.0f}) {
        for (float h = 0.0f; h <= 1.001f; h += 0.125f) {
          for (float t : {0.35f, 0.75f}) {
            Out o = Render(note, h, t, 0.45f, 0.5f, b, 2);
            if (!o.sounded) { ++lost; continue; }
            const float cents = 1200.0f * log2f(o.ratio / (0.5f * float(o.n)));
            if (fabsf(cents) > 250.0f) { ++lost; continue; }
            sum += fabs(cents); mx = std::max(mx, (double)fabsf(cents)); ++c;
          }
        }
      }
      printf("  brassy %5.1f: mean |cents| %5.1f   max %5.1f   over %d points, "
             "%d lost\n", b, c ? sum / c : 0.0, mx, c, lost);
    }
  }
  return 0;
}
