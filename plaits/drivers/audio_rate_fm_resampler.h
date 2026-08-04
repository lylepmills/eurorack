// Copyright 2026 Rubato Audio.
//
// Exact-rate bridge from the STM32F373 SDADC's 50 kHz fast mode to Plaits'
// 47.872340... kHz synthesis rate.

#ifndef PLAITS_DRIVERS_AUDIO_RATE_FM_RESAMPLER_H_
#define PLAITS_DRIVERS_AUDIO_RATE_FM_RESAMPLER_H_

#include <stddef.h>
#include <stdint.h>

namespace plaits {

// The clocks have an exact rational relationship:
//
//   50,000 / (72,000,000 / (47 * 32)) = 47 / 45.
//
// Keeping the phase as an integer numerator makes the converter drift-free.
// Linear interpolation is sufficient here because the module's documented CV
// input bandwidth is far below either Nyquist frequency.
class AudioRateFmResampler {
 public:
  static const size_t kRingBufferSize = 64;
  static const uint32_t kSourceStep = 47;
  static const uint32_t kPhaseDenominator = 45;
  // Keep enough source history to survive both audio DMA halves becoming
  // pending across a model-change/flash pause. Thirty-two 50 kHz samples add
  // 0.64 ms of FM latency and cover two back-to-back 12-sample renders even
  // when the SDADC producer has not advanced between them.
  static const size_t kNominalLag = 32;

  void Init() {
    read_index_ = 0;
    last_write_index_ = 0;
    startup_samples_ = 0;
    phase_ = 0;
    consumed_samples_ = 0;
    resync_count_ = 0;
    underflow_count_ = 0;
    excess_lag_count_ = 0;
    running_ = false;
  }

  // `write_index` is the DMA position immediately after the newest completed
  // sample. Returns false only while the ring is filling for the first time.
  // In that case the destination is still initialized with the newest scalar
  // value, so startup is silent and deterministic.
  bool Process(
      const volatile int16_t* ring,
      size_t write_index,
      float* destination,
      size_t size) {
    write_index %= kRingBufferSize;
    const size_t produced = Distance(last_write_index_, write_index);
    last_write_index_ = write_index;

    const size_t target_lag = TargetLag(size);
    if (!running_) {
      startup_samples_ += produced;
      if (startup_samples_ > kRingBufferSize) {
        startup_samples_ = kRingBufferSize;
      }
      if (startup_samples_ < target_lag) {
        const size_t latest =
            (write_index + kRingBufferSize - 1) % kRingBufferSize;
        const float value =
            static_cast<float>(ring[latest]) * (1.0f / 32768.0f);
        for (size_t i = 0; i < size; ++i) {
          destination[i] = value;
        }
        return false;
      }
      Seed(write_index, target_lag);
      running_ = true;
    } else {
      const size_t available = Distance(read_index_, write_index);
      // AudioDac deliberately services both DMA halves, oldest first, when
      // both interrupt flags are pending. In that catch-up case the FM producer
      // can be roughly two blocks ahead, and both back-to-back renders consume
      // that valid backlog. Re-seeding merely because the first render sees
      // more than one block buffered throws those samples away and guarantees
      // an underflow on the second render. Every distance below the ring's two-
      // sample interpolation guard is valid. A full-ring consumer loss cannot
      // be distinguished from no movement by a modulo DMA cursor; deliberate
      // long pauses use CvAdc's explicit realignment path instead.
      const size_t maximum_lag = kRingBufferSize - 2;
      if (available < RequiredSamples(size, phase_)) {
        Seed(write_index, target_lag);
        ++resync_count_;
        ++underflow_count_;
      } else if (available > maximum_lag) {
        Seed(write_index, target_lag);
        ++resync_count_;
        ++excess_lag_count_;
      }
    }

    for (size_t i = 0; i < size; ++i) {
      const size_t next = (read_index_ + 1) % kRingBufferSize;
      const float a = static_cast<float>(ring[read_index_]);
      const float b = static_cast<float>(ring[next]);
      const float fraction =
          static_cast<float>(phase_) /
          static_cast<float>(kPhaseDenominator);
      destination[i] =
          (a + (b - a) * fraction) * (1.0f / 32768.0f);

      phase_ += kSourceStep;
      const size_t advance = phase_ / kPhaseDenominator;
      phase_ %= kPhaseDenominator;
      read_index_ = (read_index_ + advance) % kRingBufferSize;
      consumed_samples_ += advance;
    }
    return true;
  }

  // A known blocking operation can pause the consumer while DMA continues to
  // fill the ring. Restart the normal startup acquisition from the live DMA
  // cursor instead of immediately consuming from a guessed lag: pending audio
  // callbacks can otherwise outrun that guess and manufacture an underflow.
  // Process() holds the newest scalar sample until a fresh target lag exists,
  // which takes roughly two 12-sample blocks and is inaudibly short.
  void Restart(size_t write_index) {
    write_index %= kRingBufferSize;
    last_write_index_ = write_index;
    startup_samples_ = 0;
    phase_ = 0;
    running_ = false;
  }

  inline uint32_t phase() const { return phase_; }
  inline uint32_t consumed_samples() const { return consumed_samples_; }
  inline uint32_t resync_count() const { return resync_count_; }
  inline uint32_t underflow_count() const { return underflow_count_; }
  inline uint32_t excess_lag_count() const { return excess_lag_count_; }
  inline bool running() const { return running_; }

 private:
  static size_t Distance(size_t from, size_t to) {
    return (to + kRingBufferSize - from) % kRingBufferSize;
  }

  static size_t RequiredSamples(size_t size, uint32_t phase) {
    if (!size) {
      return 0;
    }
    // The last output reads both its bracketing source samples.
    return (phase + (size - 1) * kSourceStep) /
        kPhaseDenominator + 2;
  }

  static size_t TargetLag(size_t size) {
    size_t lag = RequiredSamples(size, 0) + 3;
    if (lag < kNominalLag) {
      lag = kNominalLag;
    }
    if (lag > kRingBufferSize - 2) {
      lag = kRingBufferSize - 2;
    }
    return lag;
  }

  void Seed(size_t write_index, size_t lag) {
    read_index_ =
        (write_index + kRingBufferSize - lag) % kRingBufferSize;
    phase_ = 0;
  }

  size_t read_index_;
  size_t last_write_index_;
  size_t startup_samples_;
  uint32_t phase_;
  uint32_t consumed_samples_;
  uint32_t resync_count_;
  uint32_t underflow_count_;
  uint32_t excess_lag_count_;
  bool running_;
};

}  // namespace plaits

#endif  // PLAITS_DRIVERS_AUDIO_RATE_FM_RESAMPLER_H_
