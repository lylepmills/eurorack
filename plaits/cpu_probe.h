// Copyright 2026 Rubato Audio.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, subject to the conditions in the MIT
// license. See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// On-target CPU measurement for Plaits Lab engines (probe builds only).
//
// The audio callback gets about 1500 CPU cycles per sample (72 MHz / 48 kHz) to
// cover EVERYTHING -- synthesis, the LPG, the output stage, the UI and the ADCs.
// Overrun it and the callback cannot finish a block in time: the output glitches
// and the starved main loop stops refreshing the LEDs.
//
// A host-side estimate cannot answer whether an engine fits. A development
// machine is not merely faster; its memory system is different in kind. The
// wavetables an engine reads live in FLASH, which on this chip carries wait
// states and has NO data cache, while on a laptop they sit in L1. An engine
// doing dozens of table lookups per sample is therefore far more expensive here
// than any host timing suggests -- which is exactly how a community engine
// measured at 0.6x a stock engine on the host and still starved the module.
//
// So measure the real thing: the Cortex-M4's DWT cycle counter, wrapped around
// the real Voice::Render, inside the real audio interrupt.
//
// READOUT. The AUX output becomes a square wave whose FREQUENCY carries the
// measurement: 1000 Hz means the engine consumed the entire budget, 600 Hz means
// 60% of it, 1300 Hz means it is over by a third. Frequency rather than a DC
// level so the reading survives any gain, offset or AC coupling between the
// module and whatever measures it -- an audio interface, a tuner, a scope. MAIN
// still carries the engine's audio, so you can listen while you measure.
//
// This header compiles to NOTHING unless PLAITS_CPU_PROBE is set, so shipping
// builds are byte-identical (asserted by the SDK's build tests).

#ifndef PLAITS_CPU_PROBE_H_
#define PLAITS_CPU_PROBE_H_

#include <stddef.h>
#include <stdint.h>

#include "stmlib/stmlib.h"
#include "plaits/dsp/dsp.h"
#if !PLAITS_CPU_PROBE_HOST_TEST
#include "plaits/dsp/voice.h"
#endif

#if PLAITS_CPU_PROBE
#if PLAITS_CPU_PROBE_HOST_TEST
// Host unit test: fake the cycle counter and frame type so every line of the
// accumulate/EMA/readout logic below runs on a development machine, where it
// can be replayed deterministically and its output decoded and checked.
struct FakeDwt { uint32_t CTRL; uint32_t CYCCNT; };
struct FakeCoreDebug { uint32_t DEMCR; };
extern FakeDwt* DWT;
extern FakeCoreDebug* CoreDebug;
#define DWT_CTRL_CYCCNTENA_Msk 1u
#define CoreDebug_DEMCR_TRCENA_Msk (1u << 24)
#ifndef F_CPU
#define F_CPU 72000000L
#endif
namespace plaits { class Voice { public: struct Frame { short out; short aux; }; }; }
#else
// Only a probe build touches the debug block, so the device headers stay out of
// every other build (including the host tests, which compile this file's users).
#include <stm32f37x_conf.h>
#endif  // PLAITS_CPU_PROBE_HOST_TEST
#endif  // PLAITS_CPU_PROBE

namespace plaits {

#if PLAITS_CPU_PROBE

// The LED meter is separable from the AUX tone. A bench build that steps through
// many engines needs the normal display to keep showing WHICH engine is
// selected, so it takes the tone and leaves the LEDs alone.
#ifndef PLAITS_CPU_PROBE_LEDS
#define PLAITS_CPU_PROBE_LEDS 1
#endif

#ifndef PLAITS_CPU_PROBE_FTZ
#define PLAITS_CPU_PROBE_FTZ 0
#endif

#ifndef PLAITS_CPU_PROBE_HOST_TEST
#define PLAITS_CPU_PROBE_HOST_TEST 0
#endif

// Memhunt: the readout carries ONLY the live halfwords of the watched address,
// alternating every few seconds -- fast enough to hear the value track a knob
// or an input while someone sweeps controls. Used to identify what is writing
// into memory it does not own; see the v3/v4 investigation.
#ifndef PLAITS_CPU_PROBE_MEMHUNT
#define PLAITS_CPU_PROBE_MEMHUNT 0
#endif

// Full budget maps to this many Hz on the AUX readout.
const float kCpuProbeFullScaleHz = 1000.0f;

class CpuProbe {
 public:
  CpuProbe() { }
  ~CpuProbe() { }

  // Must run once at startup. The cycle counter is part of the debug block, so
  // the trace unit has to be powered up before CYCCNT counts at all.
  // Set FLUSH-TO-ZERO on the FPU (FPSCR bit 24) when PLAITS_CPU_PROBE_FTZ is on.
  //
  // The firmware enables the FPU but never touches FPSCR, so it runs in IEEE
  // mode and handles denormals. Any resonator or delay line decaying toward
  // silence drifts into the denormal range and STAYS there, and denormal
  // operands cost this FPU many extra cycles -- a penalty QEMU cannot see,
  // because it computes them correctly for free. inharmonic-string measured
  // 6080 cycles/sample against ~1200 predicted from its instruction count;
  // this is the test of whether that gap is denormals.
  //
  // Gated, so a probe build can measure the difference without changing what
  // ships.
  void SetFlushToZero() {
#if PLAITS_CPU_PROBE_FTZ
    uint32_t fpscr;
    __asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (1u << 24);
    __asm volatile("vmsr fpscr, %0" : : "r"(fpscr));
#endif
  }

  void Init() {
    SetFlushToZero();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    usage_ = 0.0f;
    phase_ = 0.0f;
    start_ = 0;
    for (int i = 0; i < kMaxSections; ++i) {
      section_used_[i] = false;
      section_block_[i] = 0;
      section_start_[i] = 0;
      section_usage_[i] = 0.0f;
    }
    state_ = 0;
    state_samples_ = 24000;
    report_ = 0;
    bursts_left_ = 0;
    raw_elapsed_ = 0;
    raw_section0_ = 0;
    nesting_violations_ = 0;
    blocks_ = 0;
    corruption_events_ = 0;
    guard_a_ = 0xC0DE1234u;
    guard_b_ = 0x5EC7104Eu;
  }

  inline void Begin() { start_ = DWT->CYCCNT; }

  // SECTIONS: bracket sub-parts of the render and read each one's cost
  // separately. This exists because aggregate numbers kept supporting theories
  // that were wrong -- seven consecutive explanations for one engine's 4x
  // overrun were refuted. A per-section readout asks the chip WHERE the cycles
  // go instead of asking the analyst why.
  inline void SectionBegin(int index) {
    if (index >= 0 && index < kMaxSections) {
      section_start_[index] = DWT->CYCCNT;
      section_used_[index] = true;
    }
  }
  inline void SectionEnd(int index) {
    if (index >= 0 && index < kMaxSections) {
      section_block_[index] += DWT->CYCCNT - section_start_[index];
    }
  }

  inline void End(size_t size) {
    // Unsigned arithmetic wraps correctly, so a counter rollover mid-block
    // still yields the true elapsed count.
    const uint32_t elapsed = DWT->CYCCNT - start_;
    const float budget = static_cast<float>(size) *
        (static_cast<float>(F_CPU) / kSampleRate);
    const float usage = static_cast<float>(elapsed) / budget;
    // Fast attack, slow release: a worst-case block is what makes the audio
    // glitch, so the reading must not average it away -- but it should settle
    // rather than latch forever, so a single startup transient does not sit on
    // the readout for the rest of the session.
    usage_ = usage > usage_ ? usage : usage_ * 0.9995f + usage * 0.0005f;
    // Raw same-block values, no EMA. Kept because the EMA path once reported a
    // section EXCEEDING the total that encloses it -- impossible for nested
    // brackets -- and a raw snapshot plus a violation counter is what separates
    // a reporting artifact from a genuine nesting break on the device.
    ++blocks_;
    if (guard_a_ != 0xC0DE1234u || guard_b_ != 0x5EC7104Eu) {
      ++corruption_events_;
    }
    raw_elapsed_ = elapsed;
    raw_section0_ = section_block_[0];
    if (section_used_[0] && section_block_[0] > elapsed) {
      ++nesting_violations_;
    }
    for (int i = 0; i < kMaxSections; ++i) {
      if (section_used_[i]) {
        const float share = static_cast<float>(section_block_[i]) / budget;
        section_usage_[i] = share > section_usage_[i]
            ? share
            : section_usage_[i] * 0.9995f + share * 0.0005f;
      }
      section_block_[i] = 0;
    }
  }

  // Replaces AUX with the readout tone. Called AFTER End(), so the cost of
  // generating it is excluded from the measurement -- this measures the engine,
  // not the probe.
  //
  // With no sections in use, AUX is a continuous tone at usage * 1000 Hz.
  // With sections, the readout time-multiplexes: for each report it emits
  // silence, an IDENTITY BEACON of (report + 1) short bursts, then ~1.6 s of
  // that report's tone. Report 0 is the whole render; the rest are the
  // sections, in index order. The beacon is what makes a multiplexed reading
  // decodable without trusting timing alone.
  void WriteReadout(Voice::Frame* frames, size_t size) {
    bool any_section = false;
    for (int i = 0; i < kMaxSections; ++i) {
      if (section_used_[i]) any_section = true;
    }
    for (size_t i = 0; i < size; ++i) {
      float tone_increment = 0.0f;
      bool burst = false;
      if (!any_section) {
        tone_increment = usage_ * kCpuProbeFullScaleHz / kSampleRate;
      } else {
        if (state_samples_ == 0) NextReadoutState();
        --state_samples_;
        if (state_ == 1) {
          burst = true;                       // beacon burst, fixed pitch
        } else if (state_ == 3) {
          const int used = UsedSections();
          float hz;
#if PLAITS_CPU_PROBE_MEMHUNT
          const uint32_t hot_now = *(volatile uint32_t*)0x20000814u;
          const uint32_t h = report_ == 0 ? (hot_now & 0xFFFFu) : (hot_now >> 16);
          hz = 25.0f + static_cast<float>(h >> 4);
          if (false) {
#else
          if (report_ == 0) {
#endif
            hz = usage_ * kCpuProbeFullScaleHz;
          } else if (report_ <= used) {
            hz = SectionValue(report_ - 1) * kCpuProbeFullScaleHz;
          } else if (report_ == used + 1) {
            // RAW same-block ratio: section 0 as a share of its enclosing
            // total. Nested brackets cannot legitimately exceed 1000 Hz here.
            hz = raw_elapsed_ > 0
                ? kCpuProbeFullScaleHz * static_cast<float>(raw_section0_)
                      / static_cast<float>(raw_elapsed_)
                : 0.0f;
          } else if (report_ == used + 2) {
            // Violation counter: 25 Hz means zero; each violating block adds
            // 1 Hz (clamped) -- any reading above ~30 Hz is a real break.
            float v = static_cast<float>(nesting_violations_);
            hz = 25.0f + (v > 3000.0f ? 3000.0f : v);
          } else if (report_ == used + 3) {
            // Canary check failures: 25 Hz = intact. Anything higher means this
            // object's memory is being overwritten by something else.
            float c = static_cast<float>(corruption_events_);
            hz = 25.0f + (c > 3000.0f ? 3000.0f : c);
          } else if (report_ == used + 4) {
            // End() cadence ramp: steps ~1 Hz per second of runtime when End
            // runs at block rate. Two consecutive readout cycles that show the
            // SAME value mean End has stopped updating this object.
            hz = 25.0f + static_cast<float>((blocks_ >> 12) & 2047u);
          } else if (report_ == used + 5) {
            // Last block's TOTAL, absolute (same scale as report 0). Direct,
            // EMA-free anchor to compare against the smoothed total.
            const float budget_one = static_cast<float>(F_CPU) / kSampleRate;
            hz = raw_elapsed_ > 0
                ? kCpuProbeFullScaleHz * static_cast<float>(raw_elapsed_)
                      / (12.0f * budget_one)
                : 0.0f;
          } else {
            // LIVE value of the known-stomped address (0x20000814, the probe
            // tail in the v3 layout), low then high halfword, each scaled to a
            // tone. Whatever writes there now writes into a WATCHED window:
            // when the reported value follows a knob or an input, the writer
            // has identified itself. The stomped values decoded as small,
            // slowly-decaying integers -- the bit pattern of DENORMAL FLOATS --
            // so the prime suspect is someone's filter state living at these
            // addresses through a wild or stale pointer.
#if PLAITS_CPU_PROBE_HOST_TEST
            const uint32_t hot = 0;
#else
            const uint32_t hot = *(volatile uint32_t*)0x20000814u;
#endif
            const uint32_t half = report_ == used + 6 ? (hot & 0xFFFFu) : (hot >> 16);
            hz = 25.0f + static_cast<float>(half >> 4);
          }
          if (hz < 25.0f) hz = 25.0f;         // silence must stay distinguishable
          tone_increment = hz / kSampleRate;
        }
      }
      if (burst) {
        phase_ += 2400.0f / kSampleRate;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        frames[i].aux = phase_ < 0.5f ? 16000 : -16000;
      } else if (tone_increment > 0.0f) {
        phase_ += tone_increment;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        frames[i].aux = phase_ < 0.5f ? 16000 : -16000;
      } else {
        frames[i].aux = 0;
      }
    }
  }

  inline float usage() const { return usage_; }
  inline float section_usage(int index) const {
    return index >= 0 && index < kMaxSections ? section_usage_[index] : 0.0f;
  }
  inline uint32_t nesting_violations() const { return nesting_violations_; }
  inline float raw_ratio() const {
    return raw_elapsed_
        ? static_cast<float>(raw_section0_) / static_cast<float>(raw_elapsed_)
        : 0.0f;
  }

 private:
  float SectionValue(int nth) {
    int seen = 0;
    for (int i = 0; i < kMaxSections; ++i) {
      if (section_used_[i]) {
        if (seen == nth) return section_usage_[i];
        ++seen;
      }
    }
    return 0.0f;
  }

  // states: 0 silence -> (1 burst -> 2 gap) x (report_+1) -> 3 tone -> next report
  void NextReadoutState() {
    const int kSilence = 19200;   // 0.4 s
    const int kBurst = 3360;      // 70 ms
    const int kGap = 3360;
    const int kTone = 76800;      // 1.6 s
    if (state_ == 0) {
      bursts_left_ = report_ + 1;
      state_ = 1; state_samples_ = kBurst;
    } else if (state_ == 1) {
      --bursts_left_;
      state_ = 2; state_samples_ = kGap;
    } else if (state_ == 2) {
      if (bursts_left_ > 0) { state_ = 1; state_samples_ = kBurst; }
      else { state_ = 3; state_samples_ = kTone; }
    } else {
      int reports = 1;
      for (int i = 0; i < kMaxSections; ++i) {
        if (section_used_[i]) ++reports;
      }
      if (reports > 1) reports += 7;  // + ratio, violations, canary, cadence, last-block, hot-word
#if PLAITS_CPU_PROBE_MEMHUNT
      reports = 2;                    // hot word lo/hi only, ~5 s per revisit
#endif
      report_ = (report_ + 1) % reports;
      state_ = 0; state_samples_ = kSilence;
      phase_ = 0.0f;
    }
  }

  int UsedSections() const {
    int used = 0;
    for (int i = 0; i < kMaxSections; ++i) {
      if (section_used_[i]) ++used;
    }
    return used;
  }

 public:

 private:
  static const int kMaxSections = 6;

  // Integrity canaries. The v2 diagnostics returned values this code cannot
  // produce -- a monotonic counter that DECREASED, and a raw ratio contradicting
  // the violation check computed from the same variables two lines earlier.
  // Either this object's memory is being overwritten by something else, or
  // End() is not running against the state the readout reads. The canaries
  // answer the first directly; the block-cadence ramp answers the second.
  uint32_t guard_a_;
  uint32_t start_;
  float usage_;
  float phase_;
  bool section_used_[kMaxSections];
  uint32_t section_start_[kMaxSections];
  uint32_t section_block_[kMaxSections];
  float section_usage_[kMaxSections];
  int state_;
  int state_samples_;
  int report_;
  int bursts_left_;
  uint32_t raw_elapsed_;
  uint32_t raw_section0_;
  uint32_t nesting_violations_;
  uint32_t blocks_;
  uint32_t corruption_events_;
  uint32_t guard_b_;

  DISALLOW_COPY_AND_ASSIGN(CpuProbe);
};

#define PLAITS_CPU_PROBE_DECLARE \
  plaits::CpuProbe cpu_probe; \
  extern "C" void plaits_cpu_probe_section(int index, int begin) { \
    if (begin) { cpu_probe.SectionBegin(index); } \
    else { cpu_probe.SectionEnd(index); } \
  }
#define PLAITS_CPU_PROBE_INIT cpu_probe.Init();
#define PLAITS_CPU_PROBE_BEGIN cpu_probe.Begin();
#define PLAITS_CPU_PROBE_END(size) cpu_probe.End(size);
#define PLAITS_CPU_PROBE_READOUT(frames, size) cpu_probe.WriteReadout(frames, size);
#define PLAITS_CPU_PROBE_DISPLAY(ui) (ui).DisplayCpuUsage(cpu_probe.usage());

#else

#define PLAITS_CPU_PROBE_DECLARE
#define PLAITS_CPU_PROBE_INIT
#define PLAITS_CPU_PROBE_BEGIN
#define PLAITS_CPU_PROBE_END(size)
#define PLAITS_CPU_PROBE_READOUT(frames, size)
#define PLAITS_CPU_PROBE_DISPLAY(ui)

#endif  // PLAITS_CPU_PROBE

}  // namespace plaits

#endif  // PLAITS_CPU_PROBE_H_
