// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Base class for all engines.

#ifndef PLAITS_DSP_ENGINE_ENGINE_H_
#define PLAITS_DSP_ENGINE_ENGINE_H_

#include "plaits/dsp/dsp.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/buffer_allocator.h"

// alt firmware: the stereo aux-output render path (OUT/AUX as a true L/R pair)
// is gated PER ENGINE at compile time. Each stereo-capable engine guards its
// stereo render with `if (PLAITS_STEREO_<X> && parameters.stereo)` and reports
// `stereo_capable() { return PLAITS_STEREO_<X>; }`; when a flag is 0 that
// engine's stereo code is dead-stripped and the voice never routes stereo to
// it. Every flag defaults to 1 (host tests / local / stock builds carry all of
// it); the hosted builder passes -DPLAITS_STEREO_<X>=0 per object for the
// engines a recipe leaves in mono.
#include "plaits/dsp/engine/stereo_config.h"

namespace plaits {

inline float NoteToFrequency(float midi_note) {
  midi_note -= 9.0f;
  CONSTRAIN(midi_note, -128.0f, 127.0f);
  return a0 * 0.25f * stmlib::SemitonesToRatio(midi_note);
}

// Expands a fourth macro around an engine's stock value. The midpoint is
// exactly neutral, while the two halves interpolate toward useful extremes.
inline float ApplyMacro(
    float stock_value,
    float minimum_value,
    float maximum_value,
    float macro) {
  const float amount = macro * 2.0f;
  return macro < 0.5f
      ? minimum_value + (stock_value - minimum_value) * amount
      : stock_value + (maximum_value - stock_value) * (amount - 1.0f);
}

enum TriggerState {
  TRIGGER_LOW = 0,
  TRIGGER_RISING_EDGE = 1,
  TRIGGER_UNPATCHED = 2,
  TRIGGER_HIGH = 4,
};

struct EngineParameters {
  EngineParameters()
      : articulation_envelope(0.0f),
        articulation_envelope_active(false),
        hard_sync(0),
        stereo(false) { }

  int trigger;
  float note;
  float timbre;
  float morph;
  float harmonics;
  float accent;

  // Optional fourth synthesis macro. In the alt firmware this is controlled
  // by the frequency knob while pitch is locked and option 3 is selected.
  float macro;

  // A model-aware articulation signal used by physical models that can accept
  // continuously varying excitation. Most engines deliberately ignore it and
  // retain the normal post-engine LPG or their own native envelope.
  float articulation_envelope;
  bool articulation_envelope_active;

  // alt firmware
  uint8_t chord_set_option;

  // Per-sample rising edges from the optional audio-rate sync input. Bit n
  // requests a phase reset immediately before sample n is rendered. Engines
  // reporting hard_sync_capable() consume the complete mask themselves. The
  // voice adapts every other engine through the bounded fallback below.
  uint32_t hard_sync;
  // alt firmware: when true, the voice requests a true stereo render — OUT
  // becomes the left channel and AUX the right channel. The voice only sets
  // this for engines reporting stereo_capable(); an engine that ignores the
  // flag keeps producing its regular out/aux pair. An engine built with its
  // PLAITS_STEREO_<X> flag off reports stereo_capable() == false, so the voice
  // never sets this true for it and its stereo branch is dead-stripped.
  bool stereo;
};

// Equal-power gains for one component of a stereo render.
// position 0.0 = hard left, 0.5 = center, 1.0 = hard right.
// stmlib::Sqrt compiles to a bare VSQRT: the firmware links no libm sqrtf.
inline void StereoPanGains(float position, float* left, float* right) {
  CONSTRAIN(position, 0.0f, 1.0f);
  *left = stmlib::Sqrt(1.0f - position);
  *right = stmlib::Sqrt(position);
}

// Short Schroeder all-pass for stereo paths where a second synthesis pass
// would exceed the audio deadline. Magnitude is preserved while phase is
// rotated by a frequency-dependent amount.
template<size_t delay>
class StereoPhaseAllpass {
 public:
  void Init() {
    index_ = 0;
    for (size_t i = 0; i < delay; ++i) {
      buffer_[i] = 0.0f;
    }
  }

  inline float Process(float input) {
    const float coefficient = 0.62f;
    const float delayed = buffer_[index_];
    const float output = delayed - coefficient * input;
    buffer_[index_] = input + coefficient * output;
    if (++index_ >= delay) {
      index_ = 0;
    }
    return output;
  }

 private:
  float buffer_[delay];
  size_t index_;
};

struct PostProcessingSettings {
  // A negative value indicates that a limiter must be used.
  float out_gain;
  float aux_gain;
  
  // When this flag is set to true, the engine declares that it will 
  // render a signal that already has an envelope (eg: modal drum, 808 kick).
  // By reporting this information, the synthesis voice upstream will
  // bypass the internal envelope/LPG.
  //
  // This parameter can be changed on a per-call basis when calling Render()
  // This is used by the speech synthesis engine, which renders either
  // a continuous vowel sound (which needs to be enveloped by the LPG)
  // or a word/sentence (which is already enveloped).
  bool already_enveloped;
};

class Engine {
 public:
  Engine() { }
  ~Engine() { }
  virtual void Init(stmlib::BufferAllocator* allocator) = 0;
  virtual void Reset() = 0;
  virtual void LoadUserData(const uint8_t* user_data) = 0;
  // alt firmware: length-aware variant for variable-length user banks. `length`
  // is the number of VALID packed bytes the caller baked (e.g. a short FM bank
  // of n patches is n * fm::Patch::SYX_SIZE). The base default ignores it and
  // forwards to the fixed-size version, so the many engines that don't carry a
  // bank need no change; only SixOpEngine overrides this to size its patch bank
  // (and its Harmonics quantizer) to the real count. Called through an Engine*,
  // so name lookup resolves both overloads on the base type — a derived class
  // that overrides only the single-argument version still dispatches here.
  virtual void LoadUserData(const uint8_t* user_data, size_t length) {
    (void) length;
    LoadUserData(user_data);
  }
  virtual void Render(
      const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped) = 0;
  // alt firmware: an engine that honours EngineParameters::stereo (rendering
  // OUT as the left channel and AUX as the right channel) reports it here, so
  // that the voice post-processes both channels symmetrically. Engines that
  // keep the default also keep their regular aux in stereo mode.
  virtual bool stereo_capable() const { return false; }
  // Engines opt in only after their oscillator state has an explicit,
  // sample-accurate path for every edge in the mask. Other engines use the
  // bounded first-edge fallback below.
  virtual bool hard_sync_capable() const { return false; }
  // Experimental fallback hook for engines that do not inspect trigger edges.
  // It must be cheap: the voice can call it once per audio callback while an
  // audio-rate sync source is connected. Trigger-aware engines leave this as a
  // no-op and receive a synthetic rising edge instead.
  virtual void HardSync() { }
  PostProcessingSettings post_processing_settings;
};

// Give every engine useful experimental sync behavior without making the
// highest-cost engines render once for EVERY edge in an audio callback.
//
// Native engines consume the full sample mask. The fallback honors the first
// edge exactly, renders the block in at most two pieces, calls the engine's
// light-weight HardSync hook, and injects a trigger rising edge for engines
// whose existing strike/reset behavior is their most meaningful sync action.
// Additional edges in the same callback are intentionally dropped; this caps
// fallback setup cost and makes the distinction easy to measure/audition.
inline void RenderEngineWithHardSync(
    Engine* engine,
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  if (!parameters.hard_sync || engine->hard_sync_capable()) {
    engine->Render(parameters, out, aux, size, already_enveloped);
    return;
  }

  uint32_t events = parameters.hard_sync;
  size_t edge = 0;
  while (edge < size && !(events & 1u)) {
    events >>= 1;
    ++edge;
  }

  EngineParameters segment = parameters;
  segment.hard_sync = 0;
  if (edge >= size) {
    engine->Render(segment, out, aux, size, already_enveloped);
    return;
  }

  if (edge) {
    engine->Render(segment, out, aux, edge, already_enveloped);
  }
  engine->HardSync();
  segment.trigger = TRIGGER_RISING_EDGE;
  engine->Render(
      segment,
      out + edge,
      aux + edge,
      size - edge,
      already_enveloped);
}

template<int max_size>
class EngineRegistry {
 public:
  EngineRegistry() { }
  ~EngineRegistry() { }
  
  void Init() {
    num_engines_ = 0;
  }

  inline Engine* get(int index) {
    return engine_[index];
  }
  
  void RegisterInstance(
      Engine* instance,
      bool already_enveloped,
      float out_gain,
      float aux_gain) {
    if (num_engines_ >= max_size) {
      return;
    }
    engine_[num_engines_] = instance;
    PostProcessingSettings* s = &instance->post_processing_settings;
    s->already_enveloped = already_enveloped;
    s->out_gain = out_gain;
    s->aux_gain = aux_gain;
    ++num_engines_;
  }
  
  inline int size() const { return num_engines_; }

 private:
  Engine* engine_[max_size];
  int num_engines_;
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_ENGINE_H_
