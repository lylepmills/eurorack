// Palette Seed3 firmware voice: one Plaits engine plus the module's output
// stage (registered gain / limiter) and its LPG, in float, statically
// allocated. Mirrors plugins/palette/core's EngineHost + PaletteVoice (the
// same decay/colour law as plaits::Voice) without the heap, std::function or
// the host-rate resampler, so it runs on the audio interrupt.
//
// Engine switching (SetEngine) placement-constructs ONE engine per voice into
// the voice's own storage and runs Engine::Init, so it must happen off the
// audio callback with the voice parked (see main.cc). RAM per voice is the
// 16 KB arena plus the largest engine object, not the sum of all engines.

#ifndef PALETTE_SEED3_FW_VOICE_H_
#define PALETTE_SEED3_FW_VOICE_H_

#include <new>
#include <cstddef>
#include <cstdint>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/limiter.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/envelope.h"
#include "plaits/dsp/fx/low_pass_gate.h"
#include "plaits/resources.h"

#include "engine_table.h"

namespace palette_fw {

enum UserData { kNone = 0, kFmBankA, kFmBankB, kFmBankC, kWaveTerrain };

struct EngineDesc {
  const char* id;
  const char* name;
  std::size_t size;
  plaits::Engine* (*construct)(void* storage);
  void (*destroy)(plaits::Engine* engine);
  float out_gain;
  float aux_gain;
  bool already_enveloped;
  UserData user_data;
};

template <class T>
plaits::Engine* ConstructEngine(void* storage) { return new (storage) T(); }
template <class T>
void DestroyEngine(plaits::Engine* engine) { static_cast<T*>(engine)->~T(); }

#define PALETTE_X_DESC(i, id, name, Class, og, ag, env, ud) \
  { id, name, sizeof(Class), &ConstructEngine<Class>, &DestroyEngine<Class>, og, ag, env, ud },
static const EngineDesc kEngines[] = { PALETTE_ENGINES(PALETTE_X_DESC) };
#undef PALETTE_X_DESC

constexpr std::size_t MaxOf(std::size_t a, std::size_t b) { return a > b ? a : b; }
#define PALETTE_X_SIZE(i, id, name, Class, og, ag, env, ud) MaxOf(sizeof(Class),
#define PALETTE_X_CLOSE(i, id, name, Class, og, ag, env, ud) )
constexpr std::size_t kMaxEngineSize = PALETTE_ENGINES(PALETTE_X_SIZE) 0 PALETTE_ENGINES(PALETTE_X_CLOSE);
#undef PALETTE_X_SIZE
#undef PALETTE_X_CLOSE

constexpr std::size_t kArenaBytes = 16 * 1024;   // the proven per-voice budget
constexpr std::size_t kBlockSize = plaits::kBlockSize;   // 12 frames, as the module
constexpr float kSampleRate = 48000.0f;

// The 64x64 int8 surface Wave Terrain scans when it has no user bank:
// bit-for-bit the one in plaits_test.cc / the softsynth's DefaultWaveTerrain.
const uint8_t* DefaultWaveTerrain();

const uint8_t* BuiltInUserData(UserData kind);

struct VoiceParams {
  float harmonics = 0.5f;
  float timbre = 0.5f;
  float morph = 0.5f;
  float macro = 0.5f;
  float decay = 0.5f;
  float lpg_colour = 0.5f;
  bool stereo = true;   // request L/R rendering from stereo-capable engines
  uint8_t chord_set_option = 0;
};

class FwVoice {
 public:
  FwVoice() {}
  ~FwVoice() {}

  void Init() {
    engine_ = nullptr;
    engine_index_ = -1;
    allocator_.Init(arena_, kArenaBytes);
    out_limiter_.Init();
    aux_limiter_.Init();
    lpg_envelope_.Init();
    decay_envelope_.Init();
    out_lpg_.Init();
    aux_lpg_.Init();
    note_ = 48.0f;
    gate_ = false;
    trigger_pending_ = false;
    level_ = 0.0f;
    already_enveloped_ = false;
  }

  // Off the audio thread only. Returns false for a bad index.
  bool SetEngine(int index) {
    if (engine_) {
      kEngines[engine_index_].destroy(engine_);
      engine_ = nullptr;
      engine_index_ = -1;
    }
    if (index < 0 || index >= PALETTE_ENGINE_COUNT) return false;
    const EngineDesc& d = kEngines[index];
    // The firmware's arena is .bss, so every engine sees zeros on first use;
    // zero it on each switch so a later engine cannot inherit state.
    for (std::size_t i = 0; i < kArenaBytes; ++i) arena_[i] = 0;
    for (std::size_t i = 0; i < kMaxEngineSize; ++i) engine_storage_[i] = 0;
    allocator_.Init(arena_, kArenaBytes);
    engine_ = d.construct(engine_storage_);
    engine_index_ = index;
    engine_->Init(&allocator_);
    engine_->LoadUserData(BuiltInUserData(d.user_data));
    engine_->Reset();
    out_limiter_.Init();
    aux_limiter_.Init();
    lpg_envelope_.Init();
    decay_envelope_.Init();
    out_lpg_.Init();
    aux_lpg_.Init();
    return true;
  }

  int engine_index() const { return engine_index_; }
  plaits::Engine* engine() const { return engine_; }
  bool stereo_capable() const { return engine_ && engine_->stereo_capable(); }
  bool linear_tzfm_capable() const { return engine_ && engine_->linear_tzfm_capable(); }

  void NoteOn(float note, float velocity) {
    note_ = note;
    level_ = velocity;
    gate_ = true;
    trigger_pending_ = true;
    decay_envelope_.Trigger();
  }
  void NoteOff() { gate_ = false; }
  bool gate() const { return gate_; }

  // Renders ONE 12-frame block. `render_cycles` receives the DWT cycles
  // spent inside Engine::Render alone (the number the plan asks for).
  void Render(const VoiceParams& vp, float* out, float* aux,
              uint32_t (*cycles)(), uint32_t* render_cycles,
              const float* frequency_offset = nullptr) {
    const std::size_t size = kBlockSize;
    if (!engine_) {
      for (std::size_t i = 0; i < size; ++i) { out[i] = 0.0f; aux[i] = 0.0f; }
      return;
    }
    const EngineDesc& d = kEngines[engine_index_];
    plaits::EngineParameters p;
    p.note = note_;
    p.frequency_offset = linear_tzfm_capable() ? frequency_offset : nullptr;
    p.harmonics = vp.harmonics;
    p.timbre = vp.timbre;
    p.morph = vp.morph;
    p.macro = vp.macro;
    p.chord_set_option = vp.chord_set_option;
    p.stereo = vp.stereo && engine_->stereo_capable();
    p.accent = level_;
    p.trigger = gate_ ? plaits::TRIGGER_HIGH : plaits::TRIGGER_LOW;
    if (trigger_pending_) {
      p.trigger |= plaits::TRIGGER_RISING_EDGE;
      trigger_pending_ = false;
    }

    bool enveloped = d.already_enveloped;
    const uint32_t t0 = cycles();
    engine_->Render(p, out, aux, size, &enveloped);
    *render_cycles += cycles() - t0;
    already_enveloped_ = enveloped;

    ApplyGain(d.out_gain, &out_limiter_, out, size);
    ApplyGain(d.aux_gain, &aux_limiter_, aux, size);

    // plaits::Voice's decay/colour law, as PaletteVoice carries it.
    const float block = static_cast<float>(size);
    const float short_decay = (200.0f * block) / kSampleRate *
        stmlib::SemitonesToRatio(-96.0f * vp.decay);
    decay_envelope_.Process(short_decay * 2.0f);
    if (!enveloped) {
      const float hf = vp.lpg_colour;
      const float decay_tail = (20.0f * block) / kSampleRate *
          stmlib::SemitonesToRatio(-72.0f * vp.decay + 12.0f * hf) - short_decay;
      lpg_envelope_.ProcessLP(gate_ ? level_ : 0.0f, short_decay, decay_tail, hf);
      const float gain = lpg_envelope_.gain();
      const float frequency = lpg_envelope_.frequency();
      const float hf_bleed = lpg_envelope_.hf_bleed();
      out_lpg_.Process(gain, frequency, hf_bleed, out, size);
      aux_lpg_.Process(gain, frequency, hf_bleed, aux, size);
    } else {
      lpg_envelope_.Init();
    }
  }

 private:
  static void ApplyGain(float gain, stmlib::Limiter* limiter, float* buf, std::size_t size) {
    // Voice's ChannelPostProcessor: a negative registered gain means "limit
    // with that pre-gain", a positive one is a plain multiply.
    if (gain < 0.0f) {
      limiter->Process(-gain, buf, size);
    } else {
      for (std::size_t i = 0; i < size; ++i) buf[i] *= gain;
    }
  }

  alignas(8) uint8_t arena_[kArenaBytes];
  alignas(8) uint8_t engine_storage_[kMaxEngineSize];
  stmlib::BufferAllocator allocator_;
  plaits::Engine* engine_ = nullptr;
  int engine_index_ = -1;
  stmlib::Limiter out_limiter_;
  stmlib::Limiter aux_limiter_;
  plaits::LPGEnvelope lpg_envelope_;
  plaits::DecayEnvelope decay_envelope_;
  plaits::LowPassGate out_lpg_;
  plaits::LowPassGate aux_lpg_;
  float note_ = 48.0f;
  bool gate_ = false;
  bool trigger_pending_ = false;
  float level_ = 0.0f;
  bool already_enveloped_ = false;

  FwVoice(const FwVoice&) = delete;
  FwVoice& operator=(const FwVoice&) = delete;
};

}  // namespace palette_fw

#endif  // PALETTE_SEED3_FW_VOICE_H_
