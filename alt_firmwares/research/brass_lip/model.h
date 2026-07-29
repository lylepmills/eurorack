// Lip-reed brass model, research harness.
//
// Attempt 4's structure was right but its junction was not. It used
//     p = p_minus + Zc*u
// where a waveguide junction is
//     p_plus = p_minus + Zc*u ;  p = p_plus + p_minus = 2*p_minus + Zc*u
// and it also fed the VALVE from the reflected wave rather than from the total
// junction pressure, which decouples the feedback that makes a lip lock to a
// bore mode at all. Both are fixed here.
#ifndef BRASS_MODEL_H_
#define BRASS_MODEL_H_
#include <cmath>
#include <vector>
#include <algorithm>

struct BrassParams {
  float lip_freq   = 130.0f;   // Hz
  float lip_zeta   = 0.15f;    // damping ratio
  float lip_k      = 1.0f;     // stiffness: static displacement = dp / k
  float a0         = 0.10f;    // static opening
  float a_max      = 1.00f;    // lips cannot part further than this
  float zc         = 1.00f;    // bore characteristic impedance
  float flow_gain  = 1.00f;
  float bore_len   = 367.0f;   // samples, round trip (already compensated)
  float reflect    = 0.96f;    // magnitude of the round-trip reflection
  bool  inverting  = false;    // false = complete harmonic series (adaptation)
  float damp       = 0.5f;     // bell lowpass coefficient
  float noise      = 0.004f;
  int   out_tap    = 0;   // 0 bell, 1 flow, 2 junction, 3 bell+flow
};

struct BrassModel {
  std::vector<float> d; int w = 0;
  float x = 0, v = 0;          // lip displacement, velocity
  float lp = 0;                // bell lowpass (the reflected part)
  float p_junction = 0;        // previous total junction pressure
  float dcx = 0, dcy = 0;
  float odcx = 0, odcy = 0, adcx = 0, adcy = 0;   // output taps
  unsigned rng = 22222;

  void Init(int cap) { d.assign(cap, 0.0f); w = 0; }
  void Clear() {
    std::fill(d.begin(), d.end(), 0.0f);
    w = 0; x = 0; v = 0; lp = 0; p_junction = 0; dcx = 0; dcy = 0;
    odcx = odcy = adcx = adcy = 0;
  }
  float Noise() { rng = rng*1664525u + 1013904223u; return (rng >> 8) * (2.0f/16777216.0f) - 1.0f; }

  // Returns the radiated (bell) signal.
  float Tick(const BrassParams& p, float p_mouth) {
    float rp = float(w) - p.bore_len;
    while (rp < 0) rp += d.size();
    int i0 = int(rp) % (int)d.size(), i1 = (i0 + 1) % (int)d.size();
    float fr = rp - floorf(rp);
    float arriving = d[i0] + (d[i1] - d[i0]) * fr;

    // Bell: the lowpassed part reflects, the complement radiates.
    lp += p.damp * (arriving - lp);
    float radiated = arriving - lp;
    float p_minus = (p.inverting ? -p.reflect : p.reflect) * lp;

    float mouth = p_mouth * (1.0f + p.noise * Noise());

    // Valve sees the TOTAL junction pressure, one sample late (explicit solve).
    float dp = mouth - p_junction;

    // Outward-striking lip: rising dp opens it.
    const float wl = 2.0f * float(M_PI) * p.lip_freq / 48000.0f;
    float target = dp / p.lip_k;
    v += wl * wl * (target - x) - 2.0f * p.lip_zeta * wl * v;
    x += v;

    float a = p.a0 + x;
    if (a < 0.0f) { a = 0.0f; if (x < -p.a0 && v < 0.0f) v = 0.0f; }  // lips closed
    if (a > p.a_max) a = p.a_max;

    float u = a * p.flow_gain * (dp >= 0.0f ? sqrtf(dp) : -sqrtf(-dp));

    float p_plus = p_minus + p.zc * u;
    p_junction = p_plus + p_minus;

    float bl = p_plus - dcx + 0.9995f * dcy; dcx = p_plus; dcy = bl;
    if (bl > 8.0f) bl = 8.0f; if (bl < -8.0f) bl = -8.0f;
    d[w] = bl; w = (w + 1) % d.size();
    // Blowing produces a steady DC flow as well as the oscillation; both taps
    // must be blocked or the "signal" is mostly bias.
    float fo = p.zc * u;
    float fb2 = fo - odcx + 0.9995f * odcy; odcx = fo; odcy = fb2;
    float jb = p_junction - adcx + 0.9995f * adcy; adcx = p_junction; adcy = jb;
    switch (p.out_tap) {
      case 1: return fb2;
      case 2: return jb;
      case 3: return radiated + 0.5f * fb2;
      default: return radiated;
    }
  }
};
#endif
