// Copyright 2026 Rubato Audio.
//
// Autonomous on-target stress sweep for the experimental Plaits TZFM engines.
// This is deliberately compiled only into private diagnostic firmware.

#ifndef PLAITS_TZFM_DIAGNOSTIC_H_
#define PLAITS_TZFM_DIAGNOSTIC_H_

#include <stddef.h>
#include <stdint.h>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/voice.h"

namespace plaits {

struct TzfmDiagnosticCounters {
  uint32_t overruns;
  uint32_t resyncs;
  uint32_t underflows;
  uint32_t excess_lag;
};

class TzfmDiagnostic {
 public:
  static const int kParameterSteps = 3;
  static const int kParameterStates = 27;
  static const int kPitchStates = 5;
  static const int kStimulusStates = 4;
  static const int kStatesPerStage =
      kParameterStates * kPitchStates * kStimulusStates;
  static const int kWarmupBlocks = 16;
  static const int kMeasuredBlocks = 64;
  static const int kBlocksPerState = kWarmupBlocks + kMeasuredBlocks;
  static const int kFieldsPerEngine = 10;

  struct Result {
    float peak[2];
    uint32_t measured[2];
    uint32_t over_ninety[2];
    uint32_t missed_deadline[2];
    TzfmDiagnosticCounters fast_faults;
  };

  TzfmDiagnostic() { }

  void Init(int group) {
    group_ = group;
    engine_ = 0;
    stage_ = 0;
    state_ = 0;
    block_ = 0;
    phase_ = 0.0f;
    reporting_ = false;
    report_state_ = -1;
    report_samples_ = 48000;
    report_phase_ = 0.0f;
    fast_baseline_.overruns = 0;
    fast_baseline_.resyncs = 0;
    fast_baseline_.underflows = 0;
    fast_baseline_.excess_lag = 0;
    for (int i = 0; i < PLAITS_ENGINE_COUNT; ++i) {
      results_[i].peak[0] = results_[i].peak[1] = 0.0f;
      results_[i].measured[0] = results_[i].measured[1] = 0;
      results_[i].over_ninety[0] = results_[i].over_ninety[1] = 0;
      results_[i].missed_deadline[0] =
          results_[i].missed_deadline[1] = 0;
      results_[i].fast_faults.overruns = 0;
      results_[i].fast_faults.resyncs = 0;
      results_[i].fast_faults.underflows = 0;
      results_[i].fast_faults.excess_lag = 0;
    }
  }

  bool reporting() const { return reporting_; }
  bool fast_stage() const { return !reporting_ && stage_ == 1; }
  int engine() const { return engine_; }
  int stage() const { return stage_; }
  int state() const { return state_; }
  int block() const { return block_; }
  const Result& result(int engine) const { return results_[engine]; }

  // Overwrite every panel/CV-derived synthesis value before the measured
  // bracket. Slow mode deliberately holds one FM value for the whole block;
  // Fast mode supplies the same waveform at individual sample positions.
  void Prepare(Patch* patch, Modulations* modulations, size_t size) {
    if (reporting_) return;

    const int parameter_state = state_ % kParameterStates;
    const int pitch_state =
        (state_ / kParameterStates) % kPitchStates;
    const int stimulus =
        (state_ / (kParameterStates * kPitchStates)) % kStimulusStates;
    const int harmonics_step = parameter_state % kParameterSteps;
    const int timbre_step = (parameter_state / 3) % kParameterSteps;
    const int morph_step = (parameter_state / 9) % kParameterSteps;

    patch->note = 24.0f + 21.0f * static_cast<float>(pitch_state);
    patch->harmonics = ParameterValue(harmonics_step);
    patch->timbre = ParameterValue(timbre_step);
    patch->morph = ParameterValue(morph_step);
    patch->frequency_modulation_amount = -1.0f;
    patch->timbre_modulation_amount = 0.0f;
    patch->morph_modulation_amount = 0.0f;
    patch->engine = engine_;
    patch->decay = 0.5f;
    patch->lpg_colour = 0.5f;
    patch->freqlock_param = 0.5f;
    patch->locked_frequency_pot_option = 0;
    patch->model_cv_option = 0;
    patch->level_cv_option = 0;
    patch->aux_output_option = 0;
    patch->aux_subosc_option = 0;
    patch->chord_set_option = 0;
    patch->hold_on_trigger_option = 0;
    patch->attenuverter_mode = 0;

    modulations->engine = 0.0f;
    modulations->note = 0.0f;
    modulations->frequency = 0.0f;
    modulations->harmonics = 0.0f;
    modulations->timbre = 0.0f;
    modulations->morph = 0.0f;
    modulations->trigger = 0.0f;
    modulations->level = 1.0f;
    modulations->hard_sync = 0;
    modulations->frequency_patched = true;
    modulations->frequency_audio_rate = stage_ == 1;
    modulations->timbre_patched = false;
    modulations->morph_patched = false;
    modulations->trigger_patched = false;
    modulations->level_patched = true;

    const float increment = StimulusIncrement(stimulus);
    if (stage_ == 0) {
      const float value = StimulusValue(stimulus, phase_);
      for (size_t i = 0; i < size; ++i) {
        modulations->frequency_audio[i] = value;
      }
      phase_ = WrapPhase(phase_ + increment * static_cast<float>(size));
    } else {
      for (size_t i = 0; i < size; ++i) {
        modulations->frequency_audio[i] = StimulusValue(stimulus, phase_);
        phase_ = WrapPhase(phase_ + increment);
      }
    }
  }

  void Observe(float usage, const TzfmDiagnosticCounters& counters) {
    if (reporting_) return;
    Result& result = results_[engine_];
    if (block_ >= kWarmupBlocks) {
      if (usage > result.peak[stage_]) result.peak[stage_] = usage;
      ++result.measured[stage_];
      if (usage >= 0.9f) ++result.over_ninety[stage_];
      if (usage >= 1.0f) ++result.missed_deadline[stage_];
    }

    ++block_;
    if (block_ < kBlocksPerState) return;
    block_ = 0;
    phase_ = 0.0f;
    ++state_;
    if (state_ < kStatesPerStage) return;

    state_ = 0;
    if (stage_ == 0) {
      stage_ = 1;
      fast_baseline_ = counters;
      return;
    }

    Result& finished = results_[engine_];
    finished.fast_faults.overruns =
        counters.overruns - fast_baseline_.overruns;
    finished.fast_faults.resyncs =
        counters.resyncs - fast_baseline_.resyncs;
    finished.fast_faults.underflows =
        counters.underflows - fast_baseline_.underflows;
    finished.fast_faults.excess_lag =
        counters.excess_lag - fast_baseline_.excess_lag;

    stage_ = 0;
    ++engine_;
    if (engine_ >= PLAITS_ENGINE_COUNT) {
      reporting_ = true;
      engine_ = PLAITS_ENGINE_COUNT - 1;
      report_state_ = -1;
      report_samples_ = 48000;
    }
  }

  // Silence the deliberately harsh sweep. Measurement has already ended when
  // this runs, so it cannot hide the cost of synthesis or the Fast FM ISR.
  void Mute(Voice::Frame* frames, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      frames[i].out = 0;
      frames[i].aux = 0;
    }
  }

  // Repeating, frequency-coded report on AUX. Every tone is 420 ms followed by
  // a 120 ms gap. 6000 Hz starts a frame; 6100+group*20 carries the group;
  // 6200+engine-count carries its length. Each engine then has a 5000+index*50
  // marker and ten floor+value fields. 6500 Hz ends the frame.
  void WriteReport(Voice::Frame* frames, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      if (--report_samples_ <= 0) AdvanceReport();
      float hz = ReportFrequency();
      short sample = 0;
      if (hz > 0.0f) {
        report_phase_ = WrapPhase(report_phase_ + hz / kSampleRate);
        sample = report_phase_ < 0.5f ? 16000 : -16000;
      }
      frames[i].out = 0;
      frames[i].aux = sample;
    }
  }

 private:
  static float ParameterValue(int step) {
    static const float values[3] = { 0.02f, 0.5f, 0.98f };
    return values[step];
  }

  static float WrapPhase(float phase) {
    while (phase >= 1.0f) phase -= 1.0f;
    return phase;
  }

  static float StimulusIncrement(int stimulus) {
    static const float frequencies[4] = { 40.0f, 500.0f, 4000.0f, 12000.0f };
    return frequencies[stimulus] / kSampleRate;
  }

  static float StimulusValue(int stimulus, float phase) {
    static const float amplitudes[4] = { 4.0f, 12.0f, 24.0f, 24.0f };
    if (stimulus == 1) {
      const float triangle = phase < 0.5f
          ? phase * 4.0f - 1.0f
          : 3.0f - phase * 4.0f;
      return triangle * amplitudes[stimulus];
    }
    return phase < 0.5f ? amplitudes[stimulus] : -amplitudes[stimulus];
  }

  static uint32_t UsageCode(float usage) {
    float code = usage * 1000.0f + 0.5f;
    if (code < 0.0f) code = 0.0f;
    if (code > 2000.0f) code = 2000.0f;
    return static_cast<uint32_t>(code);
  }

  static uint32_t RatioCode(uint32_t numerator, uint32_t denominator) {
    if (!denominator) return 0;
    uint32_t code = (numerator * 1000u + denominator / 2u) / denominator;
    return code > 2000u ? 2000u : code;
  }

  static uint32_t CounterCode(uint32_t value) {
    return value > 2000u ? 2000u : value;
  }

  uint32_t FieldValue(int engine, int field) const {
    const Result& r = results_[engine];
    switch (field) {
      case 0: return UsageCode(r.peak[0]);
      case 1: return UsageCode(r.peak[1]);
      case 2: return RatioCode(r.over_ninety[0], r.measured[0]);
      case 3: return RatioCode(r.over_ninety[1], r.measured[1]);
      case 4: return RatioCode(r.missed_deadline[0], r.measured[0]);
      case 5: return RatioCode(r.missed_deadline[1], r.measured[1]);
      case 6: return CounterCode(r.fast_faults.overruns);
      case 7: return CounterCode(r.fast_faults.resyncs);
      case 8: return CounterCode(r.fast_faults.underflows);
      default: return CounterCode(r.fast_faults.excess_lag);
    }
  }

  int ReportItems() const {
    return 3 + PLAITS_ENGINE_COUNT * (1 + kFieldsPerEngine) + 1;
  }

  float ReportFrequency() const {
    if (report_state_ < 0) return 0.0f;
    if ((report_state_ & 1) != 0) return 0.0f;
    const int item = report_state_ / 2;
    if (item == 0) return 6000.0f;
    if (item == 1) return 6100.0f + 20.0f * static_cast<float>(group_);
    if (item == 2) return 6200.0f + static_cast<float>(PLAITS_ENGINE_COUNT);
    if (item == ReportItems() - 1) return 6500.0f;
    const int payload = item - 3;
    const int engine = payload / (1 + kFieldsPerEngine);
    const int field = payload % (1 + kFieldsPerEngine);
    if (field == 0) return 5000.0f + 50.0f * static_cast<float>(engine);
    return 2500.0f + static_cast<float>(FieldValue(engine, field - 1));
  }

  void AdvanceReport() {
    if (report_state_ < 0) {
      report_state_ = 0;
      report_samples_ = 20160;
      report_phase_ = 0.0f;
      return;
    }
    ++report_state_;
    if (report_state_ >= ReportItems() * 2 - 1) {
      report_state_ = -1;
      report_samples_ = 48000;
    } else if ((report_state_ & 1) != 0) {
      report_samples_ = 5760;   // 120 ms silence
    } else {
      report_samples_ = 20160;  // 420 ms tone
    }
    report_phase_ = 0.0f;
  }

  int group_;
  int engine_;
  int stage_;
  int state_;
  int block_;
  float phase_;
  bool reporting_;
  Result results_[PLAITS_ENGINE_COUNT];
  TzfmDiagnosticCounters fast_baseline_;
  int report_state_;
  int report_samples_;
  float report_phase_;
};

}  // namespace plaits

#endif  // PLAITS_TZFM_DIAGNOSTIC_H_
