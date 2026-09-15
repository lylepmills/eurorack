// Copyright 2026 Lyle Mills. SPDX-License-Identifier: MIT
// Palette-only signed phase helpers. Stock Plaits never enables this option.
#ifndef PLAITS_DSP_EXTENDED_TZFM_H_
#define PLAITS_DSP_EXTENDED_TZFM_H_
#include <cmath>
#include <algorithm>
#include "stmlib/dsp/polyblep.h"
#ifndef PLAITS_BUILD_EXTENDED_TZFM
#define PLAITS_BUILD_EXTENDED_TZFM 0
#endif
#if PLAITS_BUILD_EXTENDED_TZFM && defined(STM32F37X)
#error "Extended TZFM is for Palette hosts/Seed3, not Plaits F373 firmware"
#endif
namespace plaits {
inline float TzfmWrap(float phase) {
#if PLAITS_BUILD_EXTENDED_TZFM
  return phase - floorf(phase);
#else
  return phase - static_cast<int>(phase);
#endif
}
inline float TzfmLimit(float f, float ceiling) {
  return std::max(-ceiling, std::min(ceiling, f));
}
// A value/slope discontinuity at a moving phase boundary. The unwrapped
// segment is shorter than one cycle. Both directions use the same time-domain
// BLEP; only the value jump changes sign. start/end_time locate subsegments
// split by an internal sync event within the current sample.
inline void TzfmEdge(float start, float end, float old_edge, float edge,
    float jump, float slope_jump, float* now, float* next,
    float start_time = 0.0f, float end_time = 1.0f) {
  float a = start - old_edge;
  float b = end - edge;
  if (a == b) return;
  for (int cycle = -1; cycle <= 1; ++cycle) {
    const bool forward = a < cycle && b >= cycle;
    const bool reverse = a >= cycle && b < cycle;
    if (!forward && !reverse) continue;
    const float elapsed = start_time + (end_time - start_time) *
        (cycle - a) / (b - a);
    const float t = std::max(0.0f, std::min(1.0f, 1.0f - elapsed));
    const float step = forward ? jump : -jump;
    const float slope = slope_jump * fabsf(end - start) /
        std::max(1.0e-9f, end_time - start_time);
    *now += step * stmlib::ThisBlepSample(t) +
        slope * stmlib::ThisIntegratedBlepSample(t);
    *next += step * stmlib::NextBlepSample(t) +
        slope * stmlib::NextIntegratedBlepSample(t);
  }
}
inline float TzfmSyncWave(float p, float square, float saw) {
  return (p < 0.5f ? 0.0f : square) + p * saw;
}
inline void TzfmSyncEdges(float start, float end, float square, float saw,
    float* now, float* next, float a = 0.0f, float b = 1.0f) {
  TzfmEdge(start, end, 0.5f, 0.5f, square, 0.0f, now, next, a, b);
  TzfmEdge(start, end, 0.0f, 0.0f, -square - saw, 0.0f, now, next, a, b);
}
inline void TzfmSyncStep(float mf, float sf, float square, float saw,
    float reset_phase, float* master, float* slave, float* master_now,
    float* master_next, float* slave_now, float* slave_next) {
  const float start = *master, end = start + mf;
  TzfmSyncEdges(start, end, square, saw, master_now, master_next);
  *master = TzfmWrap(end);
  *master_next += TzfmSyncWave(*master, square, saw);
  if (end >= 1.0f || end < 0.0f) {
    const float elapsed = ((mf > 0.0f ? 1.0f : 0.0f) - start) / mf;
    const float at_reset = *slave + elapsed * sf;
    TzfmSyncEdges(*slave, at_reset, square, saw, slave_now, slave_next, 0.0f, elapsed);
    const float jump = TzfmSyncWave(reset_phase, square, saw) -
        TzfmSyncWave(TzfmWrap(at_reset), square, saw);
    *slave_now += jump * stmlib::ThisBlepSample(1.0f - elapsed);
    *slave_next += jump * stmlib::NextBlepSample(1.0f - elapsed);
    const float after = reset_phase + (1.0f - elapsed) * sf;
    TzfmSyncEdges(reset_phase, after, square, saw, slave_now, slave_next, elapsed, 1.0f);
    *slave = TzfmWrap(after);
  } else {
    TzfmSyncEdges(*slave, *slave + sf, square, saw, slave_now, slave_next);
    *slave = TzfmWrap(*slave + sf);
  }
  *slave_next += TzfmSyncWave(*slave, square, saw);
}
}  // namespace plaits
#endif
