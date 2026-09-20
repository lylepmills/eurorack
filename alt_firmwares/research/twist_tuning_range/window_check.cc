#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdint>

inline float ApplyMacro(float stock, float mn, float mx, float macro) {
  const float amount = macro * 2.0f;
  return macro < 0.5f ? mn + (stock - mn) * amount
                      : stock + (mx - stock) * (amount - 1.0f);
}
static bool bitsame(float a, float b) { return !memcmp(&a,&b,4); }

int main() {
  // 1. VA dual/crossfade refactor: ApplyMacro(1,0,2,m) must be bit-identical
  //    to the shipped `macro * 2.0f` for every representable macro in [0,1].
  long mismatches = 0; const long N = 20000001;
  for (long i = 0; i < N; ++i) {
    float m = (float)((double)i / (double)(N - 1));
    float shipped = m * 2.0f;
    float refactored = ApplyMacro(1.0f, 1.0f - 1.0f, 1.0f + 1.0f, m);
    if (!bitsame(shipped, refactored)) ++mismatches;
  }
  printf("VA refactor: %ld / %ld macro values differ from `macro * 2.0f`\n", mismatches, N);

  // 2. In-tune window: how far off noon before the tuning drifts 5 cents?
  auto window = [](const char* name, float (*cents)(float)) {
    float lo = 0.0f, hi = 0.5f;
    for (int it = 0; it < 80; ++it) {
      float mid = 0.5f * (lo + hi);
      if (fabsf(cents(0.5f + mid)) < 5.0f) lo = mid; else hi = mid;
    }
    printf("  %-34s +/-%6.3f%% of travel\n", name, 100.0f * lo);
  };
  // two-op-fm: ratio in semitones, span +/-S
  static float S;
  S = 12.0f; window("two-op-fm  shipped (+/-12 st)", [](float m){ return 100.0f*(ApplyMacro(0.0f,-12.0f,12.0f,m)); });
  window("two-op-fm  narrowed (+/-1 st)",  [](float m){ return 100.0f*(ApplyMacro(0.0f,-1.0f,1.0f,m)); });
  // VA dual on a perfect fifth (7 st) and an octave (12 st)
  window("VA dual    shipped, fifth",      [](float m){ return 100.0f*7.0f*(ApplyMacro(1.0f,0.0f,2.0f,m)-1.0f); });
  window("VA dual    narrowed, fifth",     [](float m){ return 100.0f*7.0f*(ApplyMacro(1.0f,0.875f,1.125f,m)-1.0f); });
  window("VA dual    shipped, octave",     [](float m){ return 100.0f*12.0f*(ApplyMacro(1.0f,0.0f,2.0f,m)-1.0f); });
  window("VA dual    narrowed, octave",    [](float m){ return 100.0f*12.0f*(ApplyMacro(1.0f,0.875f,1.125f,m)-1.0f); });
  // phase-distortion: multiplicative ratio
  window("phase-dist shipped (x0.5..x2)",  [](float m){ return 1200.0f*log2f(ApplyMacro(1.0f,0.5f,2.0f,m)); });
  window("phase-dist narrowed (+/-1 st)",  [](float m){ return 1200.0f*log2f(ApplyMacro(1.0f,0.94387431f,1.05946309f,m)); });
  // wave-paraphonic: 12-semitone table entry
  window("wave-para  shipped, 12 st entry",[](float m){ return 100.0f*12.0f*(ApplyMacro(1.0f,0.0f,2.0f,m)-1.0f); });
  window("wave-para  narrowed, 12 st",     [](float m){ return 100.0f*12.0f*(ApplyMacro(1.0f,0.875f,1.125f,m)-1.0f); });

  // 3. Two-op FM: detune in cents across the knob, both spans.
  printf("\n  two-op-fm detune vs knob position (cents off the chosen ratio)\n");
  printf("  %-8s %12s %12s\n", "knob", "shipped", "narrowed");
  for (int i = 0; i <= 10; ++i) {
    float m = i / 10.0f;
    printf("  %6.0f%%  %11.0f  %11.1f\n", 100.0f*m,
      100.0f*ApplyMacro(0.0f,-12.0f,12.0f,m), 100.0f*ApplyMacro(0.0f,-1.0f,1.0f,m));
  }
  return 0;
}
