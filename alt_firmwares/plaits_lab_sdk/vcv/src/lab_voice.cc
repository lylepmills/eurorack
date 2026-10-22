// Copyright 2026 Matt Policastro.
// SPDX-License-Identifier: MIT
//
// Mirrors plaits/dsp/voice.cc for a single engine, with the firmware's stock
// options: TRIG response "trigger", LEVEL as level, no hold-on-trigger, no
// MODEL CV. Jack scaling follows VCV's Plaits port (Audible Instruments).

#include "lab_voice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/limiter.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/envelope.h"
#include "plaits/dsp/fx/low_pass_gate.h"
#include "plaits/dsp/physical_modelling/delay_line.h"
#include "plaits/resources.h"

#ifndef PLAITS_LAB_ENGINE_HEADER
#error PLAITS_LAB_ENGINE_HEADER must name the package engine header (see sources.py)
#endif
#include PLAITS_LAB_ENGINE_HEADER

namespace plaits_lab_vcv {

using namespace plaits;
using namespace stmlib;

namespace {

const int kMaxTriggerDelay = 8;
const int kTriggerDelay = 5;  // blocks: about 1 ms, as plaits/dsp/voice.h

// Voice::ApplyModulations.
float ApplyModulations(
    float base_value,
    float modulation_amount,
    bool use_external_modulation,
    float external_modulation,
    bool use_internal_envelope,
    float envelope,
    float default_internal_modulation,
    float minimum_value,
    float maximum_value) {
  float value = base_value;
  modulation_amount *= std::max(fabsf(modulation_amount) - 0.05f, 0.05f);
  modulation_amount *= 1.05f;
  const float modulation = use_external_modulation
      ? external_modulation
      : (use_internal_envelope ? envelope : default_internal_modulation);
  value += modulation_amount * modulation;
  CONSTRAIN(value, minimum_value, maximum_value);
  return value;
}

// ChannelPostProcessor: the same int16 the firmware writes, then the panel's
// inverting output stage, with full scale at 5 V.
class OutputStage {
 public:
  void Init() {
    lpg_.Init();
    limiter_.Init();
  }

  void Process(float gain, bool bypass_lpg, const LPGEnvelope& envelope,
               float* in, float* volts) {
    if (gain < 0.0f) {
      limiter_.Process(-gain, in, kBlockSize);
      gain = 1.0f;
    }
    const float post_gain = gain * -32767.0f;
    short frames[kBlockSize];
    if (!bypass_lpg) {
      lpg_.Process(post_gain * envelope.gain(), envelope.frequency(),
                   envelope.hf_bleed(), in, frames, kBlockSize, 1);
    } else {
      for (size_t i = 0; i < kBlockSize; ++i) {
        frames[i] = Clip16(1 + static_cast<int32_t>(in[i] * post_gain));
      }
    }
    for (size_t i = 0; i < kBlockSize; ++i) {
      volts[i] = -static_cast<float>(frames[i]) / 32768.0f * 5.0f;
    }
  }

 private:
  LowPassGate lpg_;
  Limiter limiter_;
};

}  // namespace

struct LabVoice::State {
  char allocator_memory[16384];
  PLAITS_LAB_ENGINE_CLASS engine;
  float trigger_delay_line[kMaxTriggerDelay];
  DelayLine<float, kMaxTriggerDelay> trigger_delay;
  bool trigger_state;
  float previous_note;
  DecayEnvelope decay_envelope;
  LPGEnvelope lpg_envelope;
  OutputStage out_stage;
  OutputStage aux_stage;
  float out_buffer[kBlockSize];
  float aux_buffer[kBlockSize];
};

LabVoice::LabVoice() : state_(new State) {
  State& s = *state_;
  BufferAllocator allocator(s.allocator_memory, sizeof(s.allocator_memory));
  s.engine.Init(&allocator);
#if PLAITS_LAB_USER_DATA_BANK >= 0
  s.engine.LoadUserData(fm_patches_table[PLAITS_LAB_USER_DATA_BANK]);
#else
  s.engine.LoadUserData(NULL);
#endif
  s.engine.Reset();
  s.trigger_delay.Init(s.trigger_delay_line);
  s.trigger_state = false;
  s.previous_note = 0.0f;
  s.decay_envelope.Init();
  s.lpg_envelope.Init();
  s.out_stage.Init();
  s.aux_stage.Init();
}

LabVoice::~LabVoice() { delete state_; }

const char* LabVoice::name() { return PLAITS_LAB_ENGINE_NAME; }

void LabVoice::Render(const Controls& c, const Inputs& in, float* out, float* aux) {
  State& s = *state_;

  const float modulation_note = in.v_oct * 12.0f;
  const float modulation_frequency = in.fm * 6.0f;
  const float modulation_harmonics = in.harmonics / 5.0f;
  const float modulation_timbre = in.timbre / 8.0f;
  const float modulation_morph = in.morph / 8.0f;
  const float modulation_level = in.level / 8.0f;

  // Delay TRIG by about 1 ms for sequencers whose CV lags the gate.
  s.trigger_delay.Write(in.trigger / 3.0f);
  const float trigger_value = s.trigger_delay.Read(kTriggerDelay);
  const bool previous_trigger_state = s.trigger_state;
  if (!previous_trigger_state) {
    if (trigger_value > 0.3f) {
      s.trigger_state = true;
      s.decay_envelope.Trigger();
    }
  } else if (trigger_value < 0.1f) {
    s.trigger_state = false;
  }
  const bool rising_edge = s.trigger_state && !previous_trigger_state;
  if (rising_edge && !in.level_patched) {
    s.lpg_envelope.Trigger();
  }

  EngineParameters p;
  p.macro = c.macro;
  CONSTRAIN(p.macro, 0.0f, 1.0f);
  p.chord_set_option = 0;

  const float note = (modulation_note + s.previous_note) * 0.5f;
  s.previous_note = modulation_note;

  // In Plaits LPG mode a self-enveloping engine plays untriggered, so the
  // LPG below is the only articulation.
  const bool engine_hears_trigger =
      !(c.plaits_lpg && PLAITS_LAB_ALREADY_ENVELOPED);
  if (in.trigger_patched && engine_hears_trigger) {
    p.trigger = (rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW) |
        (s.trigger_state ? TRIGGER_HIGH : TRIGGER_LOW);
  } else {
    p.trigger = TRIGGER_UNPATCHED;
  }

  const float short_decay = (200.0f * kBlockSize) / plaits::kSampleRate *
      SemitonesToRatio(-96.0f * c.decay);
  s.decay_envelope.Process(short_decay * 2.0f);
  const float internal_envelope = s.decay_envelope.value();

  float compressed_level =
      1.3f * modulation_level / (0.3f + fabsf(modulation_level));
  CONSTRAIN(compressed_level, 0.0f, 1.0f);
  p.accent = in.level_patched ? compressed_level : 0.8f;

  const bool use_internal_envelope = in.trigger_patched;

  p.harmonics = c.harmonics + modulation_harmonics;
  CONSTRAIN(p.harmonics, 0.0f, 1.0f);

  p.note = ApplyModulations(
      c.note + note,
      c.frequency_modulation_amount,
      in.fm_patched,
      modulation_frequency,
      use_internal_envelope,
      internal_envelope * internal_envelope * 48.0f,
      1.0f,
      -119.0f,
      120.0f);

  p.timbre = ApplyModulations(
      c.timbre,
      c.timbre_modulation_amount,
      in.timbre_patched,
      modulation_timbre,
      use_internal_envelope,
      internal_envelope,
      0.0f,
      0.0f,
      1.0f);

  p.morph = ApplyModulations(
      c.morph,
      c.morph_modulation_amount,
      in.morph_patched,
      modulation_morph,
      use_internal_envelope,
      internal_envelope,
      0.0f,
      0.0f,
      1.0f);

  bool already_enveloped = PLAITS_LAB_ALREADY_ENVELOPED;
  s.engine.Render(p, s.out_buffer, s.aux_buffer, kBlockSize, &already_enveloped);
  if (c.plaits_lpg) already_enveloped = false;

  const bool lpg_bypass =
      already_enveloped || (!in.level_patched && !in.trigger_patched);
  if (!lpg_bypass) {
    const float hf = c.lpg_colour;
    const float decay_tail = (20.0f * kBlockSize) / plaits::kSampleRate *
        SemitonesToRatio(-72.0f * c.decay + 12.0f * hf) - short_decay;
    if (in.level_patched) {
      s.lpg_envelope.ProcessLP(compressed_level, short_decay, decay_tail, hf);
    } else {
      const float attack = NoteToFrequency(p.note) * float(kBlockSize) * 2.0f;
      s.lpg_envelope.ProcessPing(attack, 1.0f, short_decay, decay_tail, hf);
    }
  } else {
    s.lpg_envelope.Init();
  }

  s.out_stage.Process(PLAITS_LAB_OUT_GAIN, lpg_bypass, s.lpg_envelope,
                      s.out_buffer, out);
  s.aux_stage.Process(PLAITS_LAB_AUX_GAIN, lpg_bypass, s.lpg_envelope,
                      s.aux_buffer, aux);
}

}  // namespace plaits_lab_vcv
