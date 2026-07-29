// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// Host unit test for plaits/cpu_probe.h -- every line of the accumulate, EMA
// and readout logic, replayed deterministically against a fake cycle counter.
//
// Exists because the probe reported a nested section EXCEEDING the total that
// encloses it, which is impossible for correctly paired brackets on a monotonic
// counter. This test separates "the probe logic is wrong" from "the device
// genuinely breaks nesting": scenarios 1-3 prove the logic sound under clean
// replays (including counter wrap and heavy overrun), scenario 4 proves the new
// raw-ratio/violation diagnostics DETECT a genuine nesting break, and scenario
// 5 decodes an actual generated readout stream and checks every beacon maps to
// the value it claims to carry.
//
// Build & run:
//   c++ -std=c++11 -O1 -I ../../.. -DPLAITS_CPU_PROBE=1 \
//       -DPLAITS_CPU_PROBE_AUX=1 -DPLAITS_CPU_PROBE_HOST_TEST=1 \
//       test_cpu_probe.cc -o /tmp/tcp && /tmp/tcp

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define PLAITS_CPU_PROBE 1
// Scenario 5 decodes a generated AUX stream, so this test is by definition an
// AUX-channel test — the tone is opt-in for firmware builds, never for this.
#define PLAITS_CPU_PROBE_AUX 1

#include "plaits/cpu_probe.h"

static FakeDwt g_dwt;
static FakeCoreDebug g_core_debug;
FakeDwt* DWT = &g_dwt;
FakeCoreDebug* CoreDebug = &g_core_debug;

using namespace plaits;

static int g_failures = 0;

#define CHECK(cond, msg) \
  do { \
    if (!(cond)) { \
      printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
      ++g_failures; \
    } \
  } while (0)

// One audio block: pre-engine overhead, a bracketed engine section holding
// three voice sub-sections, post-engine overhead. All in fake cycles.
static void RunBlock(CpuProbe* probe, uint32_t pre, uint32_t voice_each,
                     uint32_t engine_misc, uint32_t post) {
  probe->Begin();
  g_dwt.CYCCNT += pre;
  probe->SectionBegin(0);
  for (int v = 0; v < 3; ++v) {
    probe->SectionBegin(1 + v);
    g_dwt.CYCCNT += voice_each;
    probe->SectionEnd(1 + v);
  }
  g_dwt.CYCCNT += engine_misc;
  probe->SectionEnd(0);
  g_dwt.CYCCNT += post;
  probe->End(12);
}

int main() {
  const float kBudget = 12.0f * 1500.0f;  // cycles per 12-sample block at 100%

  // --- 1. steady nested load: EMAs must respect nesting ---
  {
    CpuProbe probe;
    probe.Init();
    for (int b = 0; b < 20000; ++b) {
      RunBlock(&probe, 500, 3600, 1200, 700);  // total 13200, section0 12000
    }
    CHECK(std::fabs(probe.usage() - 13200.0f / kBudget) < 0.01f,
          "usage converges to the replayed load");
    CHECK(probe.usage() >= probe.section_usage(0),
          "total EMA >= nested section EMA");
    CHECK(probe.section_usage(0) >= probe.section_usage(1) * 3.0f - 0.01f,
          "engine section covers its three voices");
    CHECK(probe.nesting_violations() == 0, "clean replay: no violations");
    CHECK(probe.raw_ratio() <= 1.0f, "clean replay: raw ratio <= 1");
  }

  // --- 2. counter wrap mid-block ---
  {
    CpuProbe probe;
    probe.Init();
    g_dwt.CYCCNT = 0xFFFFF000u;  // wraps during the first block
    for (int b = 0; b < 50; ++b) {
      RunBlock(&probe, 500, 3600, 1200, 700);
    }
    CHECK(probe.nesting_violations() == 0, "wrap: no false violation");
    CHECK(probe.usage() < 1.0f, "wrap: usage stays sane");
  }

  // --- 3. heavy overrun (300%) ---
  {
    CpuProbe probe;
    probe.Init();
    for (int b = 0; b < 1000; ++b) {
      RunBlock(&probe, 1000, 16000, 4000, 1000);  // total 54000 = 3x budget
    }
    CHECK(std::fabs(probe.usage() - 3.0f) < 0.05f, "overrun measured at ~300%");
    CHECK(probe.nesting_violations() == 0, "overrun alone violates nothing");
  }

  // --- 4. a GENUINE nesting break must be detected, not absorbed ---
  {
    CpuProbe probe;
    probe.Init();
    for (int b = 0; b < 100; ++b) {
      // An engine render OUTSIDE the total bracket -- the section accumulates,
      // the total never sees it. This is the only mechanism consistent with
      // the inverted reading seen on hardware; the diagnostics must flag it.
      probe.SectionBegin(0);
      g_dwt.CYCCNT += 30000;
      probe.SectionEnd(0);
      probe.Begin();
      g_dwt.CYCCNT += 10000;
      probe.End(12);
    }
    CHECK(probe.nesting_violations() == 100, "unbracketed render is counted");
    CHECK(probe.raw_ratio() > 1.0f, "raw ratio exposes the break");
  }

  // --- 5. readout decode round-trip: every beacon carries its own value ---
  {
    CpuProbe probe;
    probe.Init();
    for (int b = 0; b < 20000; ++b) {
      RunBlock(&probe, 1200, 3600, 1200, 1200);  // usage 0.8, s0 2/3, voices 0.2
    }
    // Generate ~75 s of readout and decode the two-tone ratio format: per
    // report, N bursts, a fixed-length REFERENCE tone, then a VALUE tone whose
    // length encodes the value. Both the frequency channel and the duration
    // ratio must decode to the same value.
    std::vector<int> beacon_counts;
    std::vector<double> tone_hz;      // value tone's frequency
    std::vector<double> ratio_value;  // duration-ratio-decoded value
    {
      Voice::Frame frames[12];
      int bursts = 0;
      int active_len = 0, transitions = 0;
      int ref_len = -1;
      bool in_active = false;
      short prev = 0;
      for (int b = 0; b < 400000; ++b) {
        probe.WriteReadout(frames, 12);
        for (int i = 0; i < 12; ++i) {
          const bool active = frames[i].aux != 0;
          if (active) {
            if (!in_active) {
              in_active = true;
              active_len = 0;
              transitions = 0;
              prev = frames[i].aux;
            }
            ++active_len;
            if (frames[i].aux != prev) ++transitions;
            prev = frames[i].aux;
          } else {
            if (in_active) {
              in_active = false;
              if (active_len < 7200) {          // < 0.15 s: beacon burst
                ++bursts;
              } else if (ref_len < 0) {          // first long tone: reference
                ref_len = active_len;
              } else {                           // second long tone: value
                beacon_counts.push_back(bursts);
                tone_hz.push_back(0.5 * transitions * 48000.0 / active_len);
                const double r =
                    static_cast<double>(active_len) / ref_len;
                ratio_value.push_back((r - 0.25) * 4096.0 / 0.75);
                bursts = 0;
                ref_len = -1;
              }
            }
          }
        }
      }
    }
#if PLAITS_CPU_PROBE_MEMHUNT
    // Memhunt mode: 5 reports. Beacon 1 is the known-answer constant; the
    // register reads are stubbed to 25 Hz on the host. This variant exists
    // because the mode's dispatch once silently fell through to the normal
    // chain and reported the WRONG VALUES under memhunt labels -- the exact
    // bug a mode never compiled by the test suite gets to keep.
    const int kReports = 5;
    const double expect[5] = {2629.0, 2500.0, 2500.0, 2500.0, 2500.0};
    bool seen[5] = {false, false, false, false, false};
#else
    // Expected: 12 reports -- total, 4 sections, ratio, violations, canary,
    // cadence (20000 warmup blocks >> 12 = 4 -> 29 Hz), last-block absolute,
    // and the two hot-word halves (stubbed on host).
    // Snapshot block (warmup pattern): elapsed 14400, s0 12000, voices 10800,
    // one engine call -> 125-coded 2600, ratios 833 and 750 above the floor.
    const int kReports = 16;
    const double expect[16] = {3275.0, 3141.7, 2675.0, 2675.0, 2675.0, 3308.3,
                               2500.0, 2500.0, 2504.0, 3275.0, 2600.0,
                               3333.3, 3250.0, 2500.0, 2500.0, 2500.0};
    bool seen[16] = {false, false, false, false, false, false, false, false,
                     false, false, false, false, false, false, false, false};
#endif
    for (size_t i = 0; i < beacon_counts.size(); ++i) {
      const int r = beacon_counts[i] - 1;
      if (r < 0 || r >= kReports) {
        CHECK(false, "beacon count out of range");
        continue;
      }
      seen[r] = true;
      const double err = std::fabs(tone_hz[i] - expect[r]) / expect[r];
      if (err > 0.05) {
        printf("  beacon %d: got %.1f Hz, expected %.1f Hz\n",
               r + 1, tone_hz[i], expect[r]);
        CHECK(false, "beacon tone frequency mismatch");
      }
      // The ratio channel must agree with the frequency channel: this is the
      // overrun-immune encoding, and it has to carry the same value.
      const double v_expected = expect[r] - 2500.0;
      if (std::fabs(ratio_value[i] - v_expected) > 40.0) {
        printf("  beacon %d: ratio-decoded %.0f, expected %.0f\n",
               r + 1, ratio_value[i], v_expected);
        CHECK(false, "beacon duration-ratio mismatch");
      }
    }
    for (int r = 0; r < kReports; ++r) {
      CHECK(seen[r], "every report appears in the cycle");
    }
  }

  if (g_failures == 0) {
    printf("OK: cpu_probe host test, all scenarios\n");
    return 0;
  }
  printf("%d failure(s)\n", g_failures);
  return 1;
}
