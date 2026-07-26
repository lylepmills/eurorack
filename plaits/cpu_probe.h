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
#include "plaits/dsp/voice.h"

#if PLAITS_CPU_PROBE
// Only a probe build touches the debug block, so the device headers stay out of
// every other build (including the host tests, which compile this file's users).
#include <stm32f37x_conf.h>
#endif  // PLAITS_CPU_PROBE

namespace plaits {

#if PLAITS_CPU_PROBE

// The LED meter is separable from the AUX tone. A bench build that steps through
// many engines needs the normal display to keep showing WHICH engine is
// selected, so it takes the tone and leaves the LEDs alone.
#ifndef PLAITS_CPU_PROBE_LEDS
#define PLAITS_CPU_PROBE_LEDS 1
#endif

// Full budget maps to this many Hz on the AUX readout.
const float kCpuProbeFullScaleHz = 1000.0f;

class CpuProbe {
 public:
  CpuProbe() { }
  ~CpuProbe() { }

  // Must run once at startup. The cycle counter is part of the debug block, so
  // the trace unit has to be powered up before CYCCNT counts at all.
  void Init() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    usage_ = 0.0f;
    phase_ = 0.0f;
    start_ = 0;
  }

  inline void Begin() { start_ = DWT->CYCCNT; }

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
  }

  // Replaces AUX with the readout tone. Called AFTER End(), so the cost of
  // generating it is excluded from the measurement -- this measures the engine,
  // not the probe.
  void WriteReadout(Voice::Frame* frames, size_t size) {
    const float increment = usage_ * kCpuProbeFullScaleHz / kSampleRate;
    for (size_t i = 0; i < size; ++i) {
      phase_ += increment;
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      }
      frames[i].aux = phase_ < 0.5f ? 16000 : -16000;
    }
  }

  inline float usage() const { return usage_; }

 private:
  uint32_t start_;
  float usage_;
  float phase_;

  DISALLOW_COPY_AND_ASSIGN(CpuProbe);
};

#define PLAITS_CPU_PROBE_DECLARE plaits::CpuProbe cpu_probe;
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
