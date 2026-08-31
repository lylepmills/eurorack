// Copyright 2026 Combust.
// SPDX-License-Identifier: MIT

#include "plaits/dsp/engine2/zxpulse48k_engine.h"

#include <algorithm>

namespace plaits {

using namespace stmlib;

namespace {

const float kVoiceAmp = 0.3f;
const float kEchoAmp = 0.24f;
const float kMaxDt = 0.2f;
const float kTwoOctavesDown = -24.0f;

// Stereo mode distributes the actual pin trains rather than processing a
// mono sum. The root is anchored, inner chord tones alternate across the
// field, and the arpeggio moves while its delayed ghost sits opposite.
const float kStackPan[5] = { 0.5f, 0.03f, 0.97f, 0.14f, 0.86f };
const float kArpPan[4] = { 0.03f, 0.82f, 0.18f, 0.97f };

inline float ClampDuty(float duty, float dt) {
  return std::min(std::max(duty, dt), 0.45f);
}

}  // namespace

void ZxPulse48kEngine::Init(stmlib::BufferAllocator* allocator) {
  chords_.Init(allocator);
  chords_.Reset();
  arp_mode_ = false;
  stack_count_ = 1;
  pull_ = 0.0f;
  Reset();
}

void ZxPulse48kEngine::Reset() {
  for (int v = 0; v < kNumVoices; ++v) {
    // Staggered phases so a strike never fires all pins on one sample.
    channels_[v].Init();
    for (int k = 0; k < v; ++k) {
      float dummy;
      channels_[v].Next(0.13f, 0.13f, &dummy);
    }
  }
  tick_.Init(pulse::kFrameTickPeriod);
  tick_count_ = 0;
  arp_index_ = 0;
  history_pos_ = 0;
  for (int i = 0; i < kHistorySize; ++i) {
    dt_history_[i] = 0.0f;
    width_history_[i] = 0.0f;
  }
}

void ZxPulse48kEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  // The chord comes from the firmware ChordBank: Palette chord tables
  // (including user-submitted ones) apply directly.
  chords_.set_chord(parameters.harmonics, parameters.chord_set_option);
  const float* ratios = chords_.ratios();

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Reset();
  }

  // Pin width in samples, exponential: 0.5 (whisper clicks) to ~48 (full
  // body). Width is the level control, as on the hardware that inspired it.
  const float width = 0.5f * SemitonesToRatio(79.2f * parameters.timbre);

  // MORPH low half builds the stack; high half is the arp, its rate rising
  // continuously across the span (tick/6 .. tick/1 at 50 Hz).
  const float morph = parameters.morph;
  if (!arp_mode_ && morph > 0.52f) {
    arp_mode_ = true;
  } else if (arp_mode_ && morph < 0.48f) {
    arp_mode_ = false;
  }
  int arp_divider = 1;
  if (arp_mode_) {
    float p = (morph - 0.52f) * (1.0f / 0.48f);
    CONSTRAIN(p, 0.0f, 1.0f);
    arp_divider = 6 - static_cast<int>(p * 5.49f);
  } else {
    const float raw = 1.0f + morph * (1.0f / 0.48f) * 4.49f;
    if (raw > static_cast<float>(stack_count_) + 0.6f ||
        raw < static_cast<float>(stack_count_) - 0.6f) {
      stack_count_ = static_cast<int>(raw);
      CONSTRAIN(stack_count_, 1, kNumVoices);
    }
  }

  // macro: CW = Follin echo, delay 2..11 ticks (~40..220 ms, hardware pass
  // 2026-08-25: the audible effect belongs on the positive side, and the
  // old ~100 ms cap was half a Follin echo). Noon = neutral (authentic pull
  // stays on). CCW = exaggerated dying-speaker pull.
  const float macro = parameters.macro;
  const bool echo_on = macro > 0.55f;
  const float echo_amount = echo_on ? (macro - 0.55f) * (1.0f / 0.45f) : 0.0f;
  const int echo_delay = 2 + static_cast<int>(echo_amount * 9.99f);
  const float echo_width_scale = 0.75f - 0.4f * echo_amount;
  const float pull_depth = macro < 0.45f
      ? 0.03f + (0.45f - macro) * (1.0f / 0.45f) * 0.09f
      : 0.03f;

  // Assemble voices from the chord. Stack mode: root, chord tones, root
  // octave on top. Arp mode: root + fifth drone under the arp voice.
  const float note = parameters.note + kTwoOctavesDown;
  const float f0 = NoteToFrequency(note);
  float dts[kNumVoices];
  float duties[kNumVoices];
  float amps[kNumVoices];
  int n = 0;
  int arp_slot = -1;
  if (arp_mode_) {
    dts[n] = f0;
    amps[n] = kVoiceAmp;
    ++n;
    dts[n] = f0 * 1.5f;
    amps[n] = kVoiceAmp;
    ++n;
    arp_slot = n;
    dts[n] = f0 * ratios[arp_index_ & 3] * 2.0f;
    amps[n] = kVoiceAmp;
    ++n;
  } else {
    int count = stack_count_;
    if (echo_on && count == kNumVoices) {
      --count;  // Follin: the last slot yields to the echo.
    }
    for (int v = 0; v < count; ++v) {
      dts[n] = v < 4 ? f0 * ratios[v] : f0 * ratios[0] * 2.0f;
      amps[n] = kVoiceAmp;
      ++n;
    }
  }
  // Echo ghosts the melody: the arp voice, or the TOP stack voice — the
  // root's frequency never moves at static pitch, so a root echo is a
  // near-inaudible unison (chiptune-expert review, 2026-08-15).
  const int echo_src = arp_mode_ ? arp_slot : (n > 0 ? n - 1 : 0);
  int echo_slot = -1;
  if (echo_on && n < kNumVoices) {
    echo_slot = n;
    ++n;
  }

  // Pitch pull: total pin traffic drags the whole bus flat.
  float traffic = 0.0f;
  for (int v = 0; v < n; ++v) {
    if (v == echo_slot) continue;
    CONSTRAIN(dts[v], 0.0f, kMaxDt);
    duties[v] = ClampDuty(width * dts[v], dts[v]);
    traffic += duties[v];
  }
  float load = traffic * (1.0f / 0.15f);
  CONSTRAIN(load, 0.0f, 1.5f);
  pull_ += 0.02f * (pull_depth * load - pull_);
  const float pull_ratio = SemitonesToRatio(-pull_);
  for (int v = 0; v < n; ++v) {
    if (v == echo_slot) continue;
    dts[v] *= pull_ratio;
  }

  if (echo_slot >= 0) {
    const int read = (history_pos_ - echo_delay) & (kHistorySize - 1);
    dts[echo_slot] = dt_history_[read];
    duties[echo_slot] = ClampDuty(
        width_history_[read] * echo_width_scale, dts[echo_slot]);
    amps[echo_slot] = kEchoAmp;
  }

  const bool stereo = PLAITS_STEREO_ZXPULSE48K && parameters.stereo;
  float left_gain[kNumVoices];
  float right_gain[kNumVoices];
  if (stereo) {
    const float melody_pan = kArpPan[arp_index_ & 3];
    for (int v = 0; v < n; ++v) {
      float pan;
      if (arp_mode_) {
        pan = v == 0 ? 0.16f : (v == 1 ? 0.84f : melody_pan);
      } else {
        pan = kStackPan[v];
      }
      if (v == echo_slot) {
        pan = 1.0f - (arp_mode_ ? melody_pan : kStackPan[echo_src]);
      }
      StereoPanGains(pan, &left_gain[v], &right_gain[v]);
    }
  }

  for (size_t i = 0; i < size; ++i) {
    if (tick_.Next()) {
      dt_history_[history_pos_] = dts[echo_src];
      width_history_[history_pos_] = duties[echo_src];
      history_pos_ = (history_pos_ + 1) & (kHistorySize - 1);
      ++tick_count_;
      if (arp_mode_ && (tick_count_ % static_cast<uint32_t>(arp_divider)) == 0) {
        arp_index_ = (arp_index_ + 1) & 3;
        if (arp_slot >= 0) {
          float dt = f0 * ratios[arp_index_ & 3] * 2.0f * pull_ratio;
          CONSTRAIN(dt, 0.0f, kMaxDt);
          dts[arp_slot] = dt;
          duties[arp_slot] = ClampDuty(width * dt, dt);
        }
      }
    }
    if (stereo) {
      float left = 0.0f;
      float right = 0.0f;
      for (int v = 0; v < n; ++v) {
        float naive;
        const float voice = amps[v] *
            channels_[v].Next(dts[v], duties[v], &naive);
        left += voice * left_gain[v];
        right += voice * right_gain[v];
      }
      out[i] = left;
      aux[i] = right;
    } else {
      float clean = 0.0f;
      float nasty = 0.0f;
      for (int v = 0; v < n; ++v) {
        float naive;
        clean += amps[v] * channels_[v].Next(dts[v], duties[v], &naive);
        nasty += amps[v] * naive;
      }
      out[i] = clean;
      aux[i] = nasty;
    }
  }
}

}  // namespace plaits
