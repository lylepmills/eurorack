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
// READOUT, in two independent channels -- PLAITS_CPU_PROBE_LEDS and
// PLAITS_CPU_PROBE_AUX, either of which can be built without the other.
//
// LEDS. The eight LEDs become a bottom-up bar, one per eighth of budget, with
// the next LED's brightness carrying the fraction (~1% resolution); see
// Ui::UpdateLEDs. It costs nothing but the engine-select display, which a
// one-model probe firmware has no use for anyway -- nothing is patched, no
// output is given up, and the reading is visible while you play. This is the
// channel a contributor measures with.
//
// AUX. The AUX output becomes a square wave whose FREQUENCY carries the
// measurement: 1000 Hz means the engine consumed the entire budget, 600 Hz means
// 60% of it, 1300 Hz means it is over by a third. Frequency rather than a DC
// level so the reading survives any gain, offset or AC coupling between the
// module and whatever measures it -- an audio interface, a tuner, a scope. MAIN
// still carries the engine's audio, so you can listen while you measure.
//
// The AUX channel is the precise one, and the expensive one: it OVERWRITES the
// engine's own AUX output, so a model with a real second output cannot be heard
// as it ships while the tone is on. It is also the only channel that can carry
// per-section reports or a memhunt readout, which is why the bench, validation
// and memhunt firmwares take it. It is off unless a build asks for it.
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

// ...and the tone is separable from the meter, which is the direction that
// matters for contributors: their probe build should read out without costing
// them the output their model may need. Opt-in, so the default probe leaves
// AUX alone.
#ifndef PLAITS_CPU_PROBE_AUX
#define PLAITS_CPU_PROBE_AUX 0
#endif

#if !PLAITS_CPU_PROBE_LEDS && !PLAITS_CPU_PROBE_AUX
#error "PLAITS_CPU_PROBE with neither readout channel: nothing would report."
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

#if PLAITS_CPU_PROBE_MEMHUNT && !PLAITS_CPU_PROBE_AUX
// The memhunt readout IS the tone -- the LED bar cannot carry a raw halfword.
#error "PLAITS_CPU_PROBE_MEMHUNT needs PLAITS_CPU_PROBE_AUX."
#endif

// Full budget maps to this many Hz on the AUX readout.
const float kCpuProbeFullScaleHz = 1000.0f;

// Every tone is lifted above this floor. The readout is generated in 12-sample
// blocks, and on a build that overruns its audio deadline the DAC REPLAYS
// stale blocks while the CPU catches up: a tone whose half-period fits inside
// 12 samples (>= ~2 kHz) survives replay intact, while a lower tone is chopped
// into repeating fragments that decode as stable-looking garbage. Every
// "impossible" reading in the corruption investigation -- the tripped canary,
// the decreasing counters, the nested section exceeding its parent -- was a
// tone below 2 kHz read from an overrunning build; every reading above it was
// consistent across runs. Values ride ABOVE the floor, out of the artifact
// band regardless of load.
const float kCpuProbeToneFloor = 2500.0f;

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
    last_usage_ = 0.0f;
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
    report_value_ = 0.0f;
    raw_elapsed_ = 0;
    raw_section0_ = 0;
    nesting_violations_ = 0;
    blocks_ = 0;
    corruption_events_ = 0;
    call_count_ = 0;
    snap_elapsed_ = 0;
    snap_s0_ = 0;
    snap_voices_ = 0;
    snap_calls_ = 0;
    guard_a_ = 0xC0DE1234u;
    dma_base_ = 0;
    last_write_cycles_ = 0;
    current_hz_ = 0.0f;
    guard_b_ = 0x5EC7104Eu;
    // Self-checksum of the first 32 KB of the application image, computed once
    // at boot. Every flash of this module goes through the AUDIO bootloader --
    // an acoustic channel -- and the impossible readings have been boot-to-boot
    // variable in ways that look like corrupted CODE rather than broken code.
    // The host computes the same sum over the built binary; the readout carries
    // this one. Match = the image on flash is the image we built. Mismatch =
    // the whole haunted-readings arc was bad flashing.
#if PLAITS_CPU_PROBE_HOST_TEST
    image_sum_ = 0;
#else
    {
      const volatile uint32_t* word = (const volatile uint32_t*)0x08008000u;
      uint32_t sum = 0;
      for (uint32_t i = 0; i < 8192u; ++i) {
        sum += word[i];
      }
      image_sum_ = sum;
    }
#endif
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
      if (index == 0) ++call_count_;   // engine-bracket entries this block
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
    last_usage_ = usage;
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
    // ATOMIC single-block snapshot, every 8192 blocks (~2 s): one block's
    // whole ledger captured together, so its arithmetic must balance --
    // sections cannot exceed their enclosing total WITHIN one snapshot unless
    // the nesting genuinely breaks, and the call count says outright whether
    // the engine rendered more than once inside one bracket.
    if ((blocks_ & 8191u) == 0u) {
      snap_elapsed_ = elapsed;
      snap_s0_ = section_block_[0];
      snap_voices_ = section_block_[1] + section_block_[2] + section_block_[3];
      snap_calls_ = call_count_;
    }
    call_count_ = 0;
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
  // OVERRUN-PROOF since v11. The v10 image checksum -- three boot-time
  // constants -- read back unstable, impossible values (one beacon above its
  // field's encodable maximum), which convicted the CHANNEL: under sustained
  // overrun the DMA IRQ's else-if always services TC, so buffer half 0 is
  // never refreshed and the DAC interleaves every tone with a frozen, stale
  // fragment. Two changes make the tone true at any load: phase advances by
  // WALL CLOCK (DWT cycles), not by rendered samples, and each call rewrites
  // BOTH halves of the DAC buffer, aligned to the true playhead read from the
  // DMA counter. Report DURATIONS still stretch under overrun (the state
  // machine ticks per rendered sample); the decoder classifies bursts and
  // tones by relative duration, so only the FREQUENCY needs to be honest.
  //
  // With no sections in use, AUX is a continuous tone at usage * 1000 Hz.
  // With sections, the readout time-multiplexes: for each report it emits
  // silence, an IDENTITY BEACON of (report + 1) short bursts, then ~1.6 s of
  // that report's tone. Report 0 is the whole render; the rest are the
  // sections, in index order. The beacon is what makes a multiplexed reading
  // decodable without trusting timing alone.
  void WriteReadout(Voice::Frame* frames, size_t size) {
#if !PLAITS_CPU_PROBE_HOST_TEST
    // Track the DMA buffer base: Fill() hands us alternating halves, and the
    // lower of the two pointers is the start of the whole 2*size buffer.
    if (dma_base_ == 0 || frames < dma_base_) dma_base_ = frames;
#endif
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
        } else if (state_ == 3 || state_ == 5) {
          // Both tones at floor + value: the frequency channel. The value was
          // cached at the transition into the reference tone, so it cannot
          // drift between the reference and the value tone of one report.
          tone_increment = (kCpuProbeToneFloor + report_value_) / kSampleRate;
        }
      }
      current_hz_ = burst ? 2400.0f : tone_increment * kSampleRate;
      frames[i].aux = 0;  // overwritten below on device; silent on host
#if PLAITS_CPU_PROBE_HOST_TEST
      // Host test: per-sample phase, single buffer -- the legacy behaviour the
      // decode round-trip validates.
      if (current_hz_ > 0.0f) {
        phase_ += current_hz_ / kSampleRate;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        frames[i].aux = phase_ < 0.5f ? 16000 : -16000;
      }
#endif
    }
#if !PLAITS_CPU_PROBE_HOST_TEST
    // Wall-clock phase: correct frequency regardless of how late this call is.
    const uint32_t now = DWT->CYCCNT;
    phase_ += static_cast<float>(now - last_write_cycles_)
        * (current_hz_ / static_cast<float>(F_CPU));
    last_write_cycles_ = now;
    phase_ -= static_cast<float>(static_cast<int>(phase_));
    if (dma_base_ != 0) {
      // Playhead from the DMA counter: 2 channels * 2 halves * size transfers.
      const uint32_t total = 4u * static_cast<uint32_t>(size);
      const uint32_t remaining =
          *(volatile uint32_t*)(0x40020058u + 0x04u);  // DMA1 CNDTR5
      const uint32_t played = (total - (remaining % total)) >> 1;
      const size_t frames_total = 2u * size;
      for (size_t k = 0; k < frames_total; ++k) {
        const size_t idx = (played + k) % frames_total;
        if (current_hz_ <= 0.0f) {
          dma_base_[idx].aux = 0;
          continue;
        }
        float ph = phase_ + current_hz_ * static_cast<float>(k) / kSampleRate;
        ph -= static_cast<float>(static_cast<int>(ph));
        dma_base_[idx].aux = ph < 0.5f ? 16000 : -16000;
      }
    }
#endif
  }

  inline float usage() const { return usage_; }
  // Exact cost of the most recently completed block. Automated sweep builds
  // aggregate this themselves instead of using the display-oriented EMA.
  inline float last_usage() const { return last_usage_; }
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
  float ReportHz(int report, int used) {
#if PLAITS_CPU_PROBE_MEMHUNT
    // Known-answer test first: a constant through the exact encode path.
    if (report == 0) {
      return 25.0f + static_cast<float>((0x20000814u >> 4) & 0xFFFu);
    }
#if PLAITS_CPU_PROBE_HOST_TEST
    return 25.0f;  // no device registers on the host
#else
    if (report == 1) {
      const volatile uint32_t* ccr5 = (volatile uint32_t*)0x40020058u;
      return 25.0f + static_cast<float>(*ccr5 & 0xFFFu);
    }
    const volatile uint32_t* cmar5 = (volatile uint32_t*)0x40020064u;
    const uint32_t value = *cmar5;
    uint32_t field = 0;
    if (report == 2) field = value & 0xFFFu;
    if (report == 3) field = (value >> 12) & 0xFFFu;
    if (report == 4) field = (value >> 24) & 0xFFu;
    return 25.0f + static_cast<float>(field);
#endif
#else
    if (report == 0) {
      return usage_ * kCpuProbeFullScaleHz;
    }
    if (report <= used) {
      return SectionValue(report - 1) * kCpuProbeFullScaleHz;
    }
    if (report == used + 1) {
      return raw_elapsed_ > 0
          ? kCpuProbeFullScaleHz * static_cast<float>(raw_section0_)
                / static_cast<float>(raw_elapsed_)
          : 0.0f;
    }
    if (report == used + 2) {
      float v = static_cast<float>(nesting_violations_);
      return 25.0f + (v > 3000.0f ? 3000.0f : v);
    }
    if (report == used + 3) {
      float c = static_cast<float>(corruption_events_);
      return 25.0f + (c > 3000.0f ? 3000.0f : c);
    }
    if (report == used + 4) {
      return 25.0f + static_cast<float>((blocks_ >> 12) & 2047u);
    }
    if (report == used + 5) {
      const float budget_one = static_cast<float>(F_CPU) / kSampleRate;
      return raw_elapsed_ > 0
          ? kCpuProbeFullScaleHz * static_cast<float>(raw_elapsed_)
                / (12.0f * budget_one)
          : 0.0f;
    }
    // One-block snapshot: the arithmetic that must balance.
    if (report == used + 6) {
      // Engine renders per bracket in the snapshot block. 1 render = 125.
      return 25.0f + 100.0f * static_cast<float>(snap_calls_);
    }
    if (report == used + 7) {
      // Section 0 as a share of the SAME block's elapsed. Nested => <= ~1025.
      return snap_elapsed_ > 0
          ? 25.0f + 1000.0f * static_cast<float>(snap_s0_)
                / static_cast<float>(snap_elapsed_)
          : 25.0f;
    }
    if (report == used + 8) {
      // Summed voice sections as a share of the same block's elapsed.
      return snap_elapsed_ > 0
          ? 25.0f + 1000.0f * static_cast<float>(snap_voices_)
                / static_cast<float>(snap_elapsed_)
          : 25.0f;
    }
    // Application image checksum, 12+12+8 bits across three reports. Compared
    // against the same sum computed on the host over the built binary.
    if (report == used + 9) {
      return 25.0f + static_cast<float>(image_sum_ & 0xFFFu);
    }
    if (report == used + 10) {
      return 25.0f + static_cast<float>((image_sum_ >> 12) & 0xFFFu);
    }
    return 25.0f + static_cast<float>((image_sum_ >> 24) & 0xFFu);
#endif  // PLAITS_CPU_PROBE_MEMHUNT
  }

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

  // states: 0 silence -> (1 burst -> 2 gap) x (report_+1)
  //         -> 3 REFERENCE tone (fixed length) -> 4 gap -> 5 VALUE tone
  //         (length proportional to the value) -> next report.
  //
  // The value rides on BOTH channels: the tones' frequency (fast to decode,
  // honest when the build keeps up) and the ratio of the value tone's length
  // to the reference tone's (immune to overrun: the state machine ticks per
  // rendered sample, so late rendering stretches both tones by the same
  // factor and the ratio cancels it exactly).
  void NextReadoutState() {
    const int kSilence = 19200;   // 0.4 s
    const int kBurst = 3360;      // 70 ms
    const int kGap = 3360;
    const int kRefTone = 57600;   // 1.2 s
    const int kMidGap = 12000;    // 0.25 s
    if (state_ == 0) {
      bursts_left_ = report_ + 1;
      state_ = 1; state_samples_ = kBurst;
    } else if (state_ == 1) {
      --bursts_left_;
      state_ = 2; state_samples_ = kGap;
    } else if (state_ == 2) {
      if (bursts_left_ > 0) { state_ = 1; state_samples_ = kBurst; }
      else {
        float v = ReportHz(report_, UsedSections()) - 25.0f;
        if (v < 0.0f) v = 0.0f;
        if (v > 4095.0f) v = 4095.0f;
        report_value_ = v;
        state_ = 3; state_samples_ = kRefTone;
      }
    } else if (state_ == 3) {
      state_ = 4; state_samples_ = kMidGap;
    } else if (state_ == 4) {
      state_ = 5;
      state_samples_ = 14400 + static_cast<int>(
          43200.0f * report_value_ / 4096.0f);   // 0.3 .. 1.2 s
    } else {
      int reports = 1;
      for (int i = 0; i < kMaxSections; ++i) {
        if (section_used_[i]) ++reports;
      }
      if (reports > 1) reports += 11;  // + diagnostics + snapshot + image checksum
#if PLAITS_CPU_PROBE_MEMHUNT
      reports = 5;                    // known-answer test + DAC CMAR, full width
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
  float last_usage_;
  float phase_;
  bool section_used_[kMaxSections];
  uint32_t section_start_[kMaxSections];
  uint32_t section_block_[kMaxSections];
  float section_usage_[kMaxSections];
  int state_;
  int state_samples_;
  int report_;
  int bursts_left_;
  float report_value_;
  uint32_t raw_elapsed_;
  uint32_t raw_section0_;
  uint32_t nesting_violations_;
  uint32_t blocks_;
  uint32_t corruption_events_;
  uint32_t call_count_;
  uint32_t snap_elapsed_;
  uint32_t snap_s0_;
  uint32_t snap_voices_;
  uint32_t snap_calls_;
  uint32_t image_sum_;
  Voice::Frame* dma_base_;
  uint32_t last_write_cycles_;
  float current_hz_;
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
#if PLAITS_CPU_PROBE_AUX
#define PLAITS_CPU_PROBE_READOUT(frames, size) cpu_probe.WriteReadout(frames, size);
#else
// No tone: the block leaves the audio callback exactly as the engine wrote it,
// so AUX still carries the model's own second output.
#define PLAITS_CPU_PROBE_READOUT(frames, size)
#endif
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
