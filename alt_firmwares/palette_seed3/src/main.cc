// Palette on a Daisy Seed3: engine bench + play app.
//
// Boots at 480 MHz, 48 kHz, 48-frame audio callbacks (4 x the module's
// 12-frame engine block), renders up to four FwVoices of the same engine and
// mixes them to OUT L/R. At boot it runs the per-engine CPU sweep the plan's
// §27 asks for (every engine in the table, one voice then four, cycle counter
// around Engine::Render and around the whole callback), prints one CSV line
// per engine over USB serial, then drops into play mode: a slow auto-arpeggio
// on PALETTE_PLAY_ENGINE_ID so audio out can be checked with nothing but a
// cable. Build-time knobs (see CMakeLists.txt):
//   PALETTE_VARIANT        "qspi" | "qspi_notables" | "sram"  (label only)
//   PALETTE_SKIP_BENCH     1 = go straight to play mode
//   PALETTE_PLAY_ENGINE_ID catalog id to play afterwards (default virtual-analog)

#include "daisy_seed.h"

#include <cstring>

#include "fw_voice.h"

#ifndef PALETTE_VARIANT
#define PALETTE_VARIANT "unknown"
#endif
#ifndef PALETTE_SKIP_BENCH
#define PALETTE_SKIP_BENCH 0
#endif
#ifndef PALETTE_PLAY_ENGINE_ID
#define PALETTE_PLAY_ENGINE_ID "virtual-analog"
#endif

using namespace daisy;
using namespace palette_fw;

namespace {

constexpr size_t kCallbackFrames = 48;               // 1 ms at 48 kHz
constexpr size_t kSubBlocks = kCallbackFrames / kBlockSize;
static_assert(kCallbackFrames % kBlockSize == 0, "callback must hold whole engine blocks");
constexpr int kMaxVoices = 4;
constexpr int kBenchWarmCallbacks = 32;
constexpr int kBenchMeasureCallbacks = 256;
constexpr int kRetriggerCallbacks = 96;              // ~100 ms, keeps drums firing
constexpr float kMixGain = 0.35f;

DaisySeed hw;

// Voice state lives in DTCM (zero-wait, uncached) in every variant so the
// QSPI/SRAM comparison only moves engine CODE and TABLES.
DTCM_MEM_SECTION FwVoice g_voices[kMaxVoices];
VoiceParams g_params;

// ---- ITCM: code pinned by the linker script is copied here before any
// constructor runs (the .preinit_array hook), see ld/*.lds.
extern "C" uint32_t _sitcm, _eitcm, _siitcm;
extern "C" __attribute__((section(".qspi_startup"), noinline))
void palette_copy_itcm(void) {
  uint32_t* dst = &_sitcm;
  const uint32_t* src = &_siitcm;
  while (dst < &_eitcm) *dst++ = *src++;
  __DSB();
  __ISB();
}
__attribute__((section(".preinit_array"), used))
void (*const g_preinit_copy_itcm)(void) = palette_copy_itcm;

// ---- cycle counter
inline uint32_t Cycles() { return DWT->CYCCNT; }
void StartCycleCounter() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = 0xC5ACCE55;                  // Cortex-M7 unlock
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

// ---- shared with the audio callback
enum BenchPhase { kIdle = 0, kWarm, kMeasure, kDone };
volatile int g_active_voices = 0;
volatile int g_phase = kIdle;
volatile int g_countdown = 0;
volatile uint32_t g_cb_count = 0;
volatile uint32_t g_cb_cycles_sum = 0;
volatile uint32_t g_cb_cycles_max = 0;
volatile uint32_t g_render_cycles_sum = 0;
volatile uint32_t g_render_cycles_max = 0;   // worst single Render call
volatile uint32_t g_retrigger = 0;
volatile uint32_t g_callbacks_total = 0;

const float kChord[kMaxVoices] = { 48.0f, 55.0f, 60.0f, 64.0f };

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
  (void) in;
  const uint32_t t0 = Cycles();
  const int n = g_active_voices;
  uint32_t render = 0;
  uint32_t render_max = 0;

  if (++g_retrigger >= (uint32_t) kRetriggerCallbacks) {
    g_retrigger = 0;
    for (int v = 0; v < n; ++v) g_voices[v].NoteOn(kChord[v], 0.8f);
  }

  for (size_t sub = 0; sub < size / kBlockSize; ++sub) {
    float mix_l[kBlockSize] = {0};
    float mix_r[kBlockSize] = {0};
    for (int v = 0; v < n; ++v) {
      float o[kBlockSize], a[kBlockSize];
      uint32_t r = 0;
      g_voices[v].Render(g_params, o, a, Cycles, &r);
      render += r;
      if (r > render_max) render_max = r;
      const bool stereo = g_params.stereo && g_voices[v].stereo_capable();
      for (size_t i = 0; i < kBlockSize; ++i) {
        mix_l[i] += o[i];
        mix_r[i] += stereo ? a[i] : o[i];
      }
    }
    for (size_t i = 0; i < kBlockSize; ++i) {
      OUT_L[sub * kBlockSize + i] = mix_l[i] * kMixGain;
      OUT_R[sub * kBlockSize + i] = mix_r[i] * kMixGain;
    }
  }

  ++g_callbacks_total;
  const uint32_t dt = Cycles() - t0;
  if (g_phase == kWarm) {
    if (--g_countdown <= 0) {
      g_countdown = kBenchMeasureCallbacks;
      g_cb_count = 0; g_cb_cycles_sum = 0; g_cb_cycles_max = 0;
      g_render_cycles_sum = 0; g_render_cycles_max = 0;
      g_phase = kMeasure;
    }
  } else if (g_phase == kMeasure) {
    ++g_cb_count;
    g_cb_cycles_sum += dt;
    if (dt > g_cb_cycles_max) g_cb_cycles_max = dt;
    g_render_cycles_sum += render;
    if (render_max > g_render_cycles_max) g_render_cycles_max = render_max;
    if (--g_countdown <= 0) g_phase = kDone;
  }
}

// Park the voices (the callback reads g_active_voices once per call, so one
// callback period after clearing it nothing touches them), switch engines,
// start the notes, and arm the bench phases.
void SetupVoices(int engine, int n) {
  g_active_voices = 0;
  System::Delay(4);
  for (int v = 0; v < kMaxVoices; ++v) {
    g_voices[v].Init();
    if (v < n && engine >= 0) {
      g_voices[v].SetEngine(engine);
      g_voices[v].NoteOn(kChord[v], 0.8f);
    }
  }
  g_retrigger = 0;
  g_phase = kIdle;
  g_active_voices = n;
}

void RunBenchRow(int engine, int n) {
  SetupVoices(engine, n);
  g_countdown = kBenchWarmCallbacks;
  g_phase = kWarm;
  while (g_phase != kDone) { System::Delay(1); }
  g_active_voices = 0;
  System::Delay(4);
  const uint32_t sysclk = System::GetSysClkFreq();
  const uint64_t budget = (uint64_t) sysclk * kCallbackFrames / 48000;   // cycles per callback
  const uint32_t count = g_cb_count;
  const uint32_t cb_mean = count ? g_cb_cycles_sum / count : 0;
  const uint32_t cb_max = g_cb_cycles_max;
  const uint32_t blocks = count * (n > 0 ? n : 1) * kSubBlocks;
  const uint32_t render_per_block = (n > 0 && blocks) ? g_render_cycles_sum / blocks : 0;
  const uint32_t mean_pct_x100 = (uint32_t) ((uint64_t) cb_mean * 10000 / budget);
  const uint32_t max_pct_x100 = (uint32_t) ((uint64_t) cb_max * 10000 / budget);
  const char* id = engine >= 0 ? kEngines[engine].id : "none";
  const int stereo = (engine >= 0 && n > 0) ? (g_voices[0].stereo_capable() ? 1 : 0) : 0;
  hw.PrintLine("%s,%s,%s,%d,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
               PALETTE_VARIANT, PALETTE_ENGINE_SET, id, n, stereo,
               (unsigned long) render_per_block, (unsigned long) g_render_cycles_max,
               (unsigned long) cb_mean, (unsigned long) cb_max,
               (unsigned long) mean_pct_x100, (unsigned long) max_pct_x100,
               (unsigned long) count);
}

int EngineIndexForId(const char* id) {
  for (int i = 0; i < PALETTE_ENGINE_COUNT; ++i) {
    if (std::strcmp(kEngines[i].id, id) == 0) return i;
  }
  return -1;
}

}  // namespace

int main(void) {
  hw.Init(true);   // 480 MHz
  StartCycleCounter();
  hw.SetAudioBlockSize(kCallbackFrames);
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
  for (int v = 0; v < kMaxVoices; ++v) g_voices[v].Init();
  hw.StartLog(false);
  hw.StartAudio(AudioCallback);

  const uint32_t sysclk = System::GetSysClkFreq();
  hw.PrintLine("# palette_seed3 variant=%s set=%s engines=%d sysclk=%lu block=%d voices_max=%d",
               PALETTE_VARIANT, PALETTE_ENGINE_SET, PALETTE_ENGINE_COUNT,
               (unsigned long) sysclk, (int) kCallbackFrames, kMaxVoices);
  hw.PrintLine("# board=%d (5=Seed3) itcm_bytes=%lu max_engine_bytes=%lu arena=%lu",
               (int) hw.CheckBoardVersion(),
               (unsigned long) ((uint32_t) &_eitcm - (uint32_t) &_sitcm),
               (unsigned long) kMaxEngineSize, (unsigned long) kArenaBytes);

#if !PALETTE_SKIP_BENCH
  hw.PrintLine("# columns: variant,set,engine,voices,stereo,render_cyc_per_voice_block,render_max_cyc,cb_mean_cyc,cb_max_cyc,cb_mean_pct_x100,cb_max_pct_x100,callbacks");
  const uint32_t t_start = System::GetNow();
  RunBenchRow(-1, 0);                    // empty-callback baseline
  for (int e = 0; e < PALETTE_ENGINE_COUNT; ++e) {
    hw.SetLed((e & 1) != 0);
    RunBenchRow(e, 1);
    RunBenchRow(e, kMaxVoices);
  }
  hw.PrintLine("# bench done in %lu ms, %lu callbacks",
               (unsigned long) (System::GetNow() - t_start), (unsigned long) g_callbacks_total);
#endif

  // Play mode: PALETTE_PLAY_ENGINE_ID on one voice, slow auto-arpeggio.
  int play = EngineIndexForId(PALETTE_PLAY_ENGINE_ID);
  if (play < 0) play = 0;
  SetupVoices(play, 1);
  hw.PrintLine("# playing %s (%s)", kEngines[play].id, kEngines[play].name);
  hw.SetLed(true);
  const float kArp[4] = { 48.0f, 55.0f, 60.0f, 63.0f };
  int step = 0;
  uint32_t last = System::GetNow();
  bool gate = false;
  for (;;) {
    const uint32_t now = System::GetNow();
    if (!gate && now - last >= 500) {
      g_voices[0].NoteOn(kArp[step & 3], 0.8f);
      ++step; gate = true; last = now;
    } else if (gate && now - last >= 350) {
      g_voices[0].NoteOff();
      gate = false;
    }
    System::Delay(1);
  }
}
