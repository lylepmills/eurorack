// Copyright 2026 Rubato Audio.
//
// Autonomous on-module overrun sweep. Private diagnostic firmware only: it
// takes over every synthesis-relevant panel/CV value, walks each engine in the
// build through the whole "normal use" space, and reports whether the audio
// callback ever delivered a block late enough for the DAC to play stale data --
// the one mechanism by which a CPU overrun becomes audible on Plaits.
//
// "Normal use" is everything the Plaits Palette editor offers outside its
// Experimental section (Sync In, linear TZFM, Fast FM). Without those, every
// jack is a control-rate read, so the sweep can drive the whole space
// internally with no patching: HARMONICS/TIMBRE/MORPH/TWIST, pitch, the three
// AUX output modes, and TRIG unpatched / triggered / gated (the last two put
// the outer LPG in circuit on both channels).
//
// Detectors, from most to least production-faithful:
//
//   1. Output deadline. Right after Voice::Render -- the point where
//      production firmware has written its last output sample -- the sweep
//      asks the DAC whether the DMA has already entered the half being filled.
//      If it has, production firmware would have played stale frames. The
//      sweep's own bookkeeping runs after this check, so it cannot cause it.
//   2. Callback cost: DWT cycles from callback entry to the end of Render, as
//      a fraction of the 12-sample block period. Entry latency (time between
//      the DMA event and the callback starting, read from the DMA position)
//      is reported separately, because the bracket cannot see it.
//   3. Host pilot. OUT carries a continuous 1330 Hz sine written after
//      Render, so a stale half-block shows up as a discontinuity in the ES-8
//      capture. 1330 Hz is 36 samples a cycle: a block replayed from one or
//      two DMA laps earlier (24 or 48 samples) is a third of a cycle out.
//
// Only the pilot, two DMA register reads and O(1) counters run in the audio
// interrupt. Packet building (CRC, formatting) runs in the idle main loop via
// Poll(), which the interrupt pre-empts, so the sweep costs production's
// deadline as little as possible; the mean and peak of what it does cost are
// reported.
//
// A calibration ladder runs first: a cheap engine plus a busy-wait burning a
// known fraction of the block period (80-120%), so the host can see where the
// detectors start firing on this module.
//
// Data leaves on AUX as 1200-baud FSK (mark 2400 Hz, space 4800 Hz, UART
// framing, CRC-16 packets). Packets are only sent while a cheap engine plays
// (the start, and a switch phase between engines), so an overrunning engine
// cannot corrupt them; the final report repeats the totals forever.
// plaits/tools/overrun_sweep_host.py decodes it.

#ifndef PLAITS_OVERRUN_SWEEP_H_
#define PLAITS_OVERRUN_SWEEP_H_

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "plaits/dsp/dsp.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "plaits/dsp/voice.h"

#ifndef TEST
#include <stm32f37x_conf.h>
#endif

// Engines swept: every engine in the build. Host tests may shorten it.
#ifndef PLAITS_OVERRUN_SWEEP_ENGINES
#define PLAITS_OVERRUN_SWEEP_ENGINES PLAITS_ENGINE_COUNT
#endif

#ifndef PLAITS_OVERRUN_SWEEP_GROUP
#define PLAITS_OVERRUN_SWEEP_GROUP 0
#endif

// Seconds of ring-out after each tail strike. Decay tails exist to catch
// denormals: the shipping firmware does not enable flush-to-zero, so a
// resonator or the LPG decaying toward zero can get slower, not faster.
#ifndef PLAITS_OVERRUN_SWEEP_TAIL_SECONDS
#define PLAITS_OVERRUN_SWEEP_TAIL_SECONDS 4
#endif

namespace plaits {

// Cycle counter. On target it is the DWT; host tests supply their own.
#ifdef TEST
extern uint32_t overrun_sweep_test_cycles;
// Advances on every read so busy-waits terminate.
inline uint32_t OverrunSweepCycles() {
  overrun_sweep_test_cycles += 50;
  return overrun_sweep_test_cycles;
}
inline void OverrunSweepEnableCycles() { }
#else
inline uint32_t OverrunSweepCycles() { return DWT->CYCCNT; }
inline void OverrunSweepEnableCycles() {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
#endif

#ifndef F_CPU
#define F_CPU 72000000L
#endif

inline void SaturatingAdd(uint16_t* counter, uint32_t amount) {
  const uint32_t sum = static_cast<uint32_t>(*counter) + amount;
  *counter = static_cast<uint16_t>(sum > 0xffff ? 0xffff : sum);
}

// Kept small on purpose: a 24-engine palette can sit within a few hundred
// bytes of Plaits' RAM, and these are per engine. Every counter saturates.
struct OverrunSweepStats {
  uint16_t peak;          // usage * 1000
  uint16_t late;          // blocks that missed the output deadline
  uint16_t late_states;   // distinct states with at least one late block
  uint16_t worst_state;   // state index holding `peak`
  uint16_t over_ninety;   // blocks at or above 90% of the period

  void Init() {
    peak = late = late_states = over_ninety = 0;
    worst_state = 0xffff;
  }
};

struct OverrunSweepEngineResult {
  uint8_t stereo_capable;
  OverrunSweepStats grid;
  OverrunSweepStats random;
  OverrunSweepStats tail;
  uint16_t tail_worst_block;   // block since strike at which tail peak hit
  uint16_t switch_in_peak;     // the blocks right after selecting the engine
  uint16_t switch_in_late;
  uint16_t double_pending;     // both halves due at once (a whole lap behind)
  uint16_t late_with_overhead; // late at the END of the callback, i.e.
                               // including the sweep's own bookkeeping
};

class OverrunSweep {
 public:
  enum Phase {
    PHASE_START,
    PHASE_LADDER,
    PHASE_SWITCH,
    PHASE_GRID,
    PHASE_RANDOM,
    PHASE_TAIL,
    PHASE_REPORT
  };

  enum PacketType {
    PACKET_HELLO = 1,
    PACKET_LADDER = 2,
    PACKET_ENGINE_START = 3,
    PACKET_ENGINE_RESULT = 4,
    PACKET_END = 5,
    PACKET_CONDITIONS = 6
  };

  enum Request {
    REQUEST_NONE,
    REQUEST_LADDER,   // ladder packet, then engine 0's start
    REQUEST_ENGINE,   // request_engine_'s results, then the next start
    REQUEST_FINAL     // the last engine's results, then END
  };

  static const int kVersion = 3;
  static const int kBlocksPerSecond = 3989;  // 47872.34 Hz / 12
  static const int kParamStates = 81;       // H, T, M, TWIST at 0 / .5 / 1
  static const int kPitches = 5;
  static const int kOutputs = 3;            // regular aux, stereo, sub-osc
  static const int kTriggers = 3;           // unpatched, triggered, gated
  static const int kConditions = kTriggers * kOutputs * kPitches;
  static const int kGridStates = kParamStates * kConditions;
  static const int kSettleBlocks = 8;
  static const int kStrikeBlock = 4;
  static const int kStrikeLength = 4;
  static const int kStateBlocks = 48;       // 8 settle + 40 measured
  static const int kRandomStates = 400;
  static const int kTailCases = 6;          // MORPH 0/.5/1 x mono/stereo
  static const int kTailBlocks =
      PLAITS_OVERRUN_SWEEP_TAIL_SECONDS * kBlocksPerSecond;
  static const int kStartBlocks = 2 * kBlocksPerSecond;
  static const int kLadderSteps = 21;       // 80% .. 120% in 2% steps
  static const int kLadderSettleBlocks = kBlocksPerSecond / 5;
  static const int kLadderBlocks = kBlocksPerSecond;
  static const int kSwitchBlocks = 3 * kBlocksPerSecond;
  static const int kQueueSize = 512;
  static const int kBaud = 1200;
  static const int kStatsBytes = 10;
  static const int kEngineResultBytes = 2 + 3 * kStatsBytes + 10;
  static const int kConditionBytes = 1 + kConditions * 4 + kTailCases * 6;

  OverrunSweep() { }

  void Init(int group) {
    OverrunSweepEnableCycles();
    group_ = group;
    phase_ = PHASE_START;
    engine_ = 0;
    state_ = 0;
    block_ = 0;
    stereo_capable_ = false;
    stereo_capable_known_ = true;
    for (int i = 0; i < PLAITS_OVERRUN_SWEEP_ENGINES; ++i) {
      OverrunSweepEngineResult& r = results_[i];
      r.stereo_capable = 0;
      r.grid.Init();
      r.random.Init();
      r.tail.Init();
      r.tail_worst_block = 0;
      r.switch_in_peak = 0;
      r.switch_in_late = 0;
      r.double_pending = 0;
      r.late_with_overhead = 0;
    }
    for (int i = 0; i < kLadderSteps; ++i) {
      ladder_peak_[i] = 0;
      ladder_late_[i] = 0;
      ladder_late_with_overhead_[i] = 0;
    }
    ResetConditions();
    last_state_late_ = -1;
    last_late_total_ = 0;
    last_double_total_ = 0;
    // Pilot: a rotating phasor, a few multiply-adds per sample.
    const float w = 2.0f * 3.14159265f * PilotHz() / kCorrectedSampleRate;
    pilot_cos_ = cosf(w);
    pilot_sin_ = sinf(w);
    pilot_x_ = 1.0f;
    pilot_y_ = 0.0f;
    mark_increment_ = MarkHz() / kCorrectedSampleRate;
    space_increment_ = SpaceHz() / kCorrectedSampleRate;
    bit_increment_ = static_cast<float>(kBaud) / kCorrectedSampleRate;
    tone_phase_ = 0.0f;
    bit_phase_ = 0.0f;
    queue_head_ = queue_tail_ = 0;
    shift_ = 0;
    bits_left_ = 0;
    lead_in_bits_ = kLeadInBits;
    trail_bits_ = kTrailBits;
    fsk_active_ = false;
    current_bit_ = 1;
    report_item_ = 0;
    report_gap_ = 0;
    callback_start_ = 0;
    render_end_ = 0;
    usage_ = 0.0f;
    target_load_ = 0.0f;
    total_blocks_ = 0;
    max_overhead_ = 0;
    max_entry_lag_ = 0;
    overhead_sum_ = 0;
    overhead_count_ = 0;
    settings_key_ = -1;
    request_ = REQUEST_NONE;
    request_engine_ = 0;
    final_ = false;
    EnqueueHello();
  }

  // Idle main loop: builds the packets the interrupt asked for. The interrupt
  // only sets `request_`; it never touches the queue's producer side while a
  // request is pending, and it does not start the next engine's measurement
  // (or the report) until the switch phase -- seconds -- has elapsed.
  void Poll() {
    const Request request = request_;
    if (request == REQUEST_NONE) return;
    const int engine = request_engine_;
    if (request == REQUEST_LADDER) {
      EnqueueLadder();
    } else {
      EnqueueEngineResult(engine);
      EnqueueConditions(engine);
      ResetConditions();
    }
    if (request == REQUEST_FINAL) {
      EnqueueEnd();
    } else {
      EnqueueEngineStart(request == REQUEST_LADDER ? 0 : engine + 1);
    }
    request_ = REQUEST_NONE;
  }

  bool reporting() const { return phase_ == PHASE_REPORT; }
  Phase phase() const { return phase_; }
  int engine() const { return engine_; }
  int state() const { return state_; }
  int block() const { return block_; }
  const OverrunSweepEngineResult& result(int engine) const {
    return results_[engine];
  }
  uint16_t ladder_peak(int i) const { return ladder_peak_[i]; }
  uint16_t ladder_late(int i) const { return ladder_late_[i]; }
  float last_usage() const { return usage_; }
  uint16_t condition_late(int c) const { return condition_late_[c]; }
  uint16_t max_overhead() const { return max_overhead_; }

  // Called at the very top of the audio callback. `entry_lag` is how many
  // frames the DMA had already played of the other half when the callback
  // started: time the cost bracket cannot see.
  inline void BeginCallback(uint32_t entry_lag) {
    callback_start_ = OverrunSweepCycles();
    if (entry_lag > max_entry_lag_ && phase_ != PHASE_REPORT) {
      max_entry_lag_ = static_cast<uint16_t>(entry_lag);
    }
  }

  // Called right after Voice::Render. During the ladder it burns cycles up to
  // the target fraction of the block period, standing in for an engine of
  // known cost; then it closes the cost measurement. The caller samples the
  // output deadline immediately after this returns.
  inline void EndRender(size_t size) {
    const float budget =
        static_cast<float>(size) * (static_cast<float>(F_CPU) / kSampleRate);
    if (phase_ == PHASE_LADDER && target_load_ > 0.0f) {
      const uint32_t target = static_cast<uint32_t>(budget * target_load_);
      while (OverrunSweepCycles() - callback_start_ < target) { }
    }
    render_end_ = OverrunSweepCycles();
    usage_ = static_cast<float>(render_end_ - callback_start_) / budget;
  }

  // Called last in the callback: how long the sweep's own work after Render
  // took, as a fraction of the period (reported, so its bias is known).
  inline void EndCallback(size_t size) {
    const float budget =
        static_cast<float>(size) * (static_cast<float>(F_CPU) / kSampleRate);
    const uint16_t overhead = UsageCode(
        static_cast<float>(OverrunSweepCycles() - render_end_) / budget);
    if (phase_ != PHASE_REPORT) {
      if (overhead > max_overhead_) max_overhead_ = overhead;
      overhead_sum_ += overhead;
      ++overhead_count_;
    }
  }

  // Overwrites every synthesis-relevant patch and modulation value for the
  // coming block. Runs after Ui::Poll, so the UI's real per-block cost stays
  // in the measurement while its readings are discarded.
  void Prepare(Patch* patch, Modulations* modulations) {
    const Settings& s = CurrentSettings();

    patch->engine = s.engine;
    patch->note = s.note;
    patch->harmonics = s.harmonics;
    patch->timbre = s.timbre;
    patch->morph = s.morph;
    patch->frequency_modulation_amount = 0.0f;
    patch->timbre_modulation_amount = 0.0f;
    patch->morph_modulation_amount = 0.0f;
    patch->decay = 0.5f;
    patch->lpg_colour = 0.5f;
    patch->freqlock_param = s.twist;
    patch->locked_frequency_pot_option = 1;  // TWIST on the FREQUENCY knob
    patch->trig_response_option = s.trigger == 2 ? 1 : 0;
    patch->model_cv_option = 0;
    patch->level_cv_option = 0;
    patch->aux_output_option = static_cast<uint8_t>(s.output);
    patch->aux_subosc_option = 1;  // square, one octave down
    patch->chord_set_option = 0;
    patch->hold_on_trigger_option = 0;
    patch->attenuverter_mode = 0;

    modulations->engine = 0.0f;
    modulations->note = 0.0f;
    modulations->frequency = 0.0f;
    modulations->harmonics = 0.0f;
    modulations->timbre = 0.0f;
    modulations->morph = 0.0f;
    modulations->level = 0.0f;
    modulations->hard_sync = 0;
    modulations->frequency_audio_rate = false;
    modulations->frequency_patched = false;
    modulations->timbre_patched = false;
    modulations->morph_patched = false;
    modulations->level_patched = false;
    modulations->trigger_patched = s.trigger != 0;
    modulations->trigger = s.trigger_high ? 1.0f : 0.0f;
  }

  // Called once per callback, after EndRender. `output_late` is the deadline
  // sampled right after Render (detector 1) and belongs to this block. The
  // DAC's end-of-callback counters are only updated after the callback
  // returns, so their deltas describe the previous block; they are kept per
  // engine only, where a one-block lag does not matter.
  void Observe(
      bool output_late,
      uint32_t late_total,
      uint32_t double_total,
      bool active_engine_stereo_capable) {
    if (phase_ == PHASE_REPORT) return;
    const uint32_t late_after = late_total - last_late_total_;
    const uint32_t doubled = double_total - last_double_total_;
    last_late_total_ = late_total;
    last_double_total_ = double_total;

    const uint16_t usage = UsageCode(usage_);
    switch (phase_) {
      case PHASE_LADDER:
        if (block_ >= kLadderSettleBlocks && usage > ladder_peak_[state_]) {
          ladder_peak_[state_] = usage;
        }
        if (output_late) SaturatingAdd(&ladder_late_[state_], 1);
        SaturatingAdd(&ladder_late_with_overhead_[state_],
                      late_after + doubled);
        break;
      case PHASE_GRID:
      case PHASE_RANDOM:
      case PHASE_TAIL:
        RecordEngineBlock(usage, output_late, late_after, doubled);
        break;
      default:
        break;
    }
    stereo_capable_ = active_engine_stereo_capable;
    ++total_blocks_;
    Advance();
  }

  // OUT: continuous pilot sine (the host's stale-block detector). AUX: the
  // engine's own AUX / stereo-right render, except while an FSK packet is on
  // the air.
  void WriteOutputs(Voice::Frame* frames, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      const float x = pilot_x_ * pilot_cos_ - pilot_y_ * pilot_sin_;
      const float y = pilot_y_ * pilot_cos_ + pilot_x_ * pilot_sin_;
      pilot_x_ = x;
      pilot_y_ = y;
      frames[i].out = static_cast<short>(y * 16000.0f);
      if (Transmitting()) {
        frames[i].aux = FskSample();
      }
    }
    // Hold the phasor on the unit circle.
    const float gain =
        1.5f - 0.5f * (pilot_x_ * pilot_x_ + pilot_y_ * pilot_y_);
    pilot_x_ *= gain;
    pilot_y_ *= gain;
  }

  // Final report: OUT silent, AUX repeats every totals packet with a gap
  // between passes, forever.
  void WriteReport(Voice::Frame* frames, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      if (!Transmitting()) {
        if (report_gap_ > 0) {
          --report_gap_;
        } else {
          EnqueueReportItem();
        }
      }
      frames[i].out = 0;
      frames[i].aux = Transmitting() ? FskSample() : 0;
    }
  }

  // The LED progress bar only needs refreshing occasionally.
  bool progress_due() const { return (total_blocks_ & 255) == 0; }

  float progress() const {
    if (phase_ == PHASE_REPORT) return 1.0f;
    if (phase_ == PHASE_START || phase_ == PHASE_LADDER) return 0.0f;
    return (static_cast<float>(engine_) + PhaseFraction()) /
        static_cast<float>(PLAITS_OVERRUN_SWEEP_ENGINES);
  }

  bool passed() const { return FailureMask() == 0; }

  // Bit 0: some normal-use block reached 90% of the period (the top LED pair
  // -- headroom). Bit 1: some block missed the output deadline (middle pair).
  uint8_t FailureMask() const {
    uint8_t mask = 0;
    for (int i = 0; i < PLAITS_OVERRUN_SWEEP_ENGINES; ++i) {
      const OverrunSweepEngineResult& r = results_[i];
      if (r.grid.peak >= 900 || r.random.peak >= 900 || r.tail.peak >= 900) {
        mask |= 1;
      }
      if (r.grid.late || r.random.late || r.tail.late) {
        mask |= 2;
      }
    }
    return mask;
  }

  // ---- Schedule, public for the host test and the decoder's mirror. ----

  struct Settings {
    int engine;
    float note;
    float harmonics;
    float timbre;
    float morph;
    float twist;
    int output;
    int trigger;
    bool trigger_high;
  };

  static float Level(int step) {
    return step == 0 ? 0.0f : (step == 1 ? 0.5f : 1.0f);
  }

  static float Pitch(int step) {
    return 12.0f + 24.0f * static_cast<float>(step);
  }

  // Grid state index: param + 81 * (pitch + 5 * (output + 3 * trigger)),
  // param = H + 3 T + 9 M + 27 TWIST.
  static void GridState(int index, Settings* s) {
    const int param = index % kParamStates;
    const int pitch = (index / kParamStates) % kPitches;
    s->output = (index / (kParamStates * kPitches)) % kOutputs;
    s->trigger = index / (kParamStates * kPitches * kOutputs);
    s->harmonics = Level(param % 3);
    s->timbre = Level((param / 3) % 3);
    s->morph = Level((param / 9) % 3);
    s->twist = Level(param / 27);
    s->note = Pitch(pitch);
  }

  // Condition: trigger, AUX output, pitch bucket -- the axes most likely to
  // decide whether an engine fits, reported per engine.
  static int Condition(int trigger, int output, float note) {
    int pitch = static_cast<int>((note - 12.0f) / 24.0f + 0.5f);
    if (pitch < 0) pitch = 0;
    if (pitch >= kPitches) pitch = kPitches - 1;
    return (trigger * kOutputs + output) * kPitches + pitch;
  }

  static uint32_t NextRandom(uint32_t* x) {
    *x = *x * 1664525u + 1013904223u;
    return *x;
  }

  static float RandomUnit(uint32_t* x) {
    return static_cast<float>(NextRandom(x) >> 8) * (1.0f / 16777215.0f);
  }

  // Random state `index` of engine `engine`: an independent LCG stream per
  // state, so the decoder can regenerate any single state.
  static void RandomState(int engine, int index, Settings* s) {
    uint32_t x = 0x9e3779b9u * static_cast<uint32_t>(engine + 1) +
        0x85ebca6bu * static_cast<uint32_t>(index + 1);
    NextRandom(&x);
    s->harmonics = RandomUnit(&x);
    s->timbre = RandomUnit(&x);
    s->morph = RandomUnit(&x);
    s->twist = RandomUnit(&x);
    s->note = 12.0f + 96.0f * RandomUnit(&x);
    s->output = static_cast<int>(NextRandom(&x) >> 16) % kOutputs;
    s->trigger = static_cast<int>(NextRandom(&x) >> 16) % kTriggers;
  }

  // Tail case: MORPH 0 / .5 / 1, then mono / stereo.
  static void TailState(int index, Settings* s) {
    s->harmonics = 0.5f;
    s->timbre = 0.5f;
    s->twist = 0.5f;
    s->morph = Level(index % 3);
    s->output = index < 3 ? 0 : 1;
    s->trigger = 1;
    s->note = 36.0f;
  }

  static float LadderLoad(int step) {
    return 0.8f + 0.02f * static_cast<float>(step);
  }

  static uint16_t CrcUpdate(uint16_t crc, uint8_t byte) {
    crc ^= static_cast<uint16_t>(byte) << 8;
    for (int i = 0; i < 8; ++i) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
    return crc;
  }

 private:
  static float PilotHz() { return 1330.0f; }
  static float MarkHz() { return 2400.0f; }
  static float SpaceHz() { return 4800.0f; }
  static const int kLeadInBits = 24;
  static const int kTrailBits = 12;
  static const int kSampleRateInt = 47872;

  static uint16_t UsageCode(float usage) {
    float code = usage * 1000.0f + 0.5f;
    if (code < 0.0f) code = 0.0f;
    if (code > 65535.0f) code = 65535.0f;
    return static_cast<uint16_t>(code);
  }

  float PhaseFraction() const {
    switch (phase_) {
      case PHASE_GRID:
        return 0.6f * static_cast<float>(state_) / kGridStates;
      case PHASE_RANDOM:
        return 0.6f + 0.1f * static_cast<float>(state_) / kRandomStates;
      case PHASE_TAIL:
        return 0.7f + 0.3f * static_cast<float>(state_) / kTailCases;
      default: return 0.0f;
    }
  }

  // Settings change once per state (every 48 blocks); only the trigger level
  // moves per block. Cached so the interrupt does not redo the divisions and
  // random draws each block.
  const Settings& CurrentSettings() {
    const int key = (phase_ * 64 + engine_) * 16384 + state_;
    if (key != settings_key_) {
      settings_key_ = key;
      Settings* s = &settings_;
      s->engine = engine_;
      switch (phase_) {
        case PHASE_GRID:
          GridState(state_, s);
          break;
        case PHASE_RANDOM:
          RandomState(engine_, state_, s);
          break;
        case PHASE_TAIL:
          TailState(state_, s);
          break;
        default:
          // Start, ladder and the switch phase between engines: engine 0 (a
          // deliberately cheap engine) at rest, so packets go out intact.
          s->engine = 0;
          GridState(40, s);  // all four macros centred
          s->note = 60.0f;
          break;
      }
      if ((phase_ == PHASE_GRID || phase_ == PHASE_RANDOM) &&
          !stereo_capable_ && s->output == 1) {
        s->output = 0;  // identical render; see SkipGridState
      }
      condition_ = Condition(s->trigger, s->output, s->note);
    }
    Settings* s = &settings_;
    s->trigger_high = false;
    if (s->trigger == 1) {
      s->trigger_high = block_ >= kStrikeBlock &&
          block_ < kStrikeBlock + kStrikeLength;
    } else if (s->trigger == 2) {
      s->trigger_high = block_ >= kStrikeBlock;
    }
    return settings_;
  }

  void ResetConditions() {
    for (int i = 0; i < kConditions; ++i) {
      condition_late_[i] = 0;
      condition_peak_[i] = 0;
    }
    for (int i = 0; i < kTailCases; ++i) {
      tail_case_peak_[i] = 0;
      tail_case_late_[i] = 0;
      tail_case_final_peak_[i] = 0;
    }
  }

  void RecordEngineBlock(
      uint16_t usage, bool late, uint32_t late_after, uint32_t doubled) {
    OverrunSweepEngineResult& r = results_[engine_];
    SaturatingAdd(&r.late_with_overhead, late_after + doubled);
    SaturatingAdd(&r.double_pending, doubled);

    // The first blocks after selecting the engine carry the model switch
    // itself (LoadUserData, Reset): normal use, but reported on its own.
    if (phase_ == PHASE_GRID && state_ == 0 && block_ < kSettleBlocks) {
      if (usage > r.switch_in_peak) r.switch_in_peak = usage;
      if (late) SaturatingAdd(&r.switch_in_late, 1);
      return;
    }

    OverrunSweepStats* stats = phase_ == PHASE_GRID
        ? &r.grid : (phase_ == PHASE_RANDOM ? &r.random : &r.tail);
    // Settle blocks follow an abrupt jump to new settings -- like a CV step --
    // so a late settle block counts; only the cost peak skips them.
    const bool measured = block_ >= kSettleBlocks;
    if (measured) {
      if (usage >= 900) SaturatingAdd(&stats->over_ninety, 1);
      if (usage > stats->peak) {
        stats->peak = usage;
        stats->worst_state = static_cast<uint16_t>(state_);
        if (phase_ == PHASE_TAIL) {
          r.tail_worst_block = static_cast<uint16_t>(
              block_ > 0xffff ? 0xffff : block_);
        }
      }
    }
    if (late) {
      SaturatingAdd(&stats->late, 1);
      const int key = phase_ * 8192 + state_;
      if (key != last_state_late_) {
        last_state_late_ = key;
        SaturatingAdd(&stats->late_states, 1);
      }
    }

    if (phase_ == PHASE_TAIL) {
      if (measured && usage > tail_case_peak_[state_]) {
        tail_case_peak_[state_] = usage;
      }
      if (block_ >= kTailBlocks - kBlocksPerSecond &&
          usage > tail_case_final_peak_[state_]) {
        tail_case_final_peak_[state_] = usage;
      }
      if (late) SaturatingAdd(&tail_case_late_[state_], 1);
      return;
    }
    const int c = condition_;
    if (measured && usage > condition_peak_[c]) condition_peak_[c] = usage;
    if (late) SaturatingAdd(&condition_late_[c], 1);
  }

  // Grid states whose stereo render equals the regular one are skipped
  // entirely for engines without a stereo path.
  bool SkipGridState(int index) const {
    if (stereo_capable_) return false;
    return ((index / (kParamStates * kPitches)) % kOutputs) == 1;
  }

  bool SkipTailState(int index) const {
    return !stereo_capable_ && index >= 3;
  }

  void Advance() {
    ++block_;
    switch (phase_) {
      case PHASE_START:
        if (block_ >= kStartBlocks) {
          phase_ = PHASE_LADDER;
          state_ = 0;
          block_ = 0;
          target_load_ = LadderLoad(0);
        }
        break;

      case PHASE_LADDER:
        if (block_ >= kLadderSettleBlocks + kLadderBlocks) {
          block_ = 0;
          ++state_;
          if (state_ >= kLadderSteps) {
            target_load_ = 0.0f;
            request_engine_ = 0;
            request_ = REQUEST_LADDER;
            BeginEngine(0);
          } else {
            target_load_ = LadderLoad(state_);
          }
        }
        break;

      case PHASE_SWITCH:
        if (block_ >= kSwitchBlocks && final_) {
          // The last engine's packets have been built and queued by Poll();
          // WriteReport waits for them to drain before the report loop.
          phase_ = PHASE_REPORT;
          report_item_ = 0;
          report_gap_ = kSampleRateInt;
        } else if (block_ >= kSwitchBlocks) {
          phase_ = PHASE_GRID;
          state_ = 0;
          block_ = 0;
          // Latched from the tested engine's first rendered block.
          stereo_capable_known_ = false;
        }
        break;

      case PHASE_GRID:
        if (!stereo_capable_known_) {
          // The first grid block rendered the tested engine; its capability
          // decides which grid states run.
          stereo_capable_known_ = true;
          results_[engine_].stereo_capable = stereo_capable_ ? 1 : 0;
        }
        if (block_ >= kStateBlocks) {
          block_ = 0;
          do {
            ++state_;
          } while (state_ < kGridStates && SkipGridState(state_));
          if (state_ >= kGridStates) {
            phase_ = PHASE_RANDOM;
            state_ = 0;
          }
        }
        break;

      case PHASE_RANDOM:
        if (block_ >= kStateBlocks) {
          block_ = 0;
          ++state_;
          if (state_ >= kRandomStates) {
            phase_ = PHASE_TAIL;
            state_ = 0;
          }
        }
        break;

      case PHASE_TAIL:
        if (block_ >= kTailBlocks) {
          block_ = 0;
          do {
            ++state_;
          } while (state_ < kTailCases && SkipTailState(state_));
          if (state_ >= kTailCases) {
            request_engine_ = engine_;
            if (engine_ + 1 < PLAITS_OVERRUN_SWEEP_ENGINES) {
              request_ = REQUEST_ENGINE;
              BeginEngine(engine_ + 1);
            } else {
              // Park on the cheap engine while Poll() queues the last
              // packets, then report.
              request_ = REQUEST_FINAL;
              final_ = true;
              phase_ = PHASE_SWITCH;
              state_ = 0;
              block_ = 0;
            }
          }
        }
        break;

      default:
        break;
    }
  }

  void BeginEngine(int engine) {
    engine_ = engine;
    phase_ = PHASE_SWITCH;
    state_ = 0;
    block_ = 0;
    last_state_late_ = -1;
    // Poll() queues the start packet and resets the condition table (after
    // sending the previous engine's) during this switch phase.
  }

  // ---- Packets ----

  void Push(uint8_t byte) {
    const int next = (queue_tail_ + 1) % kQueueSize;
    if (next == queue_head_) return;  // full: drop (the report repeats)
    queue_[queue_tail_] = byte;
    queue_tail_ = next;
  }

  void PushCrc(uint8_t byte, uint16_t* crc) {
    Push(byte);
    *crc = CrcUpdate(*crc, byte);
  }

  void Push16(uint16_t value, uint16_t* crc) {
    PushCrc(static_cast<uint8_t>(value & 0xff), crc);
    PushCrc(static_cast<uint8_t>(value >> 8), crc);
  }

  void BeginPacket(uint8_t type, uint8_t length, uint16_t* crc) {
    Push(0x55);
    Push(0x55);
    Push(0x7e);
    Push(0xa5);
    *crc = 0xffff;
    PushCrc(type, crc);
    PushCrc(length, crc);
  }

  void EndPacket(uint16_t crc) {
    Push(static_cast<uint8_t>(crc & 0xff));
    Push(static_cast<uint8_t>(crc >> 8));
  }

  void EnqueueHello() {
    uint16_t crc;
    BeginPacket(PACKET_HELLO, 8, &crc);
    PushCrc(kVersion, &crc);
    PushCrc(static_cast<uint8_t>(group_), &crc);
    PushCrc(PLAITS_OVERRUN_SWEEP_ENGINES, &crc);
    PushCrc(kLadderSteps, &crc);
    Push16(static_cast<uint16_t>(kGridStates), &crc);
    Push16(static_cast<uint16_t>(kRandomStates), &crc);
    EndPacket(crc);
  }

  void EnqueueLadder() {
    uint16_t crc;
    BeginPacket(PACKET_LADDER, kLadderSteps * 6, &crc);
    for (int i = 0; i < kLadderSteps; ++i) {
      Push16(ladder_peak_[i], &crc);
      Push16(ladder_late_[i], &crc);
      Push16(ladder_late_with_overhead_[i], &crc);
    }
    EndPacket(crc);
  }

  void EnqueueEngineStart(int engine) {
    uint16_t crc;
    BeginPacket(PACKET_ENGINE_START, 1, &crc);
    PushCrc(static_cast<uint8_t>(engine), &crc);
    EndPacket(crc);
  }

  void PushStats(const OverrunSweepStats& s, uint16_t* crc) {
    Push16(s.peak, crc);
    Push16(s.late, crc);
    Push16(s.late_states, crc);
    Push16(s.worst_state, crc);
    Push16(s.over_ninety, crc);
  }

  void EnqueueEngineResult(int engine) {
    const OverrunSweepEngineResult& r = results_[engine];
    uint16_t crc;
    BeginPacket(PACKET_ENGINE_RESULT, kEngineResultBytes, &crc);
    PushCrc(static_cast<uint8_t>(engine), &crc);
    PushCrc(r.stereo_capable, &crc);
    PushStats(r.grid, &crc);
    PushStats(r.random, &crc);
    PushStats(r.tail, &crc);
    Push16(r.tail_worst_block, &crc);
    Push16(r.switch_in_peak, &crc);
    Push16(r.switch_in_late, &crc);
    Push16(r.double_pending, &crc);
    Push16(r.late_with_overhead, &crc);
    EndPacket(crc);
  }

  // Sent once, live, at the end of each engine: the per-condition breakdown
  // and the tail cases. Only the current engine's table is kept in RAM.
  void EnqueueConditions(int engine) {
    uint16_t crc;
    BeginPacket(PACKET_CONDITIONS, kConditionBytes, &crc);
    PushCrc(static_cast<uint8_t>(engine), &crc);
    for (int i = 0; i < kConditions; ++i) {
      Push16(condition_peak_[i], &crc);
      Push16(condition_late_[i], &crc);
    }
    for (int i = 0; i < kTailCases; ++i) {
      Push16(tail_case_peak_[i], &crc);
      Push16(tail_case_late_[i], &crc);
      Push16(tail_case_final_peak_[i], &crc);
    }
    EndPacket(crc);
  }

  void EnqueueEnd() {
    uint16_t crc;
    BeginPacket(PACKET_END, 11, &crc);
    PushCrc(FailureMask(), &crc);
    Push16(static_cast<uint16_t>(total_blocks_ & 0xffff), &crc);
    Push16(static_cast<uint16_t>(total_blocks_ >> 16), &crc);
    Push16(max_overhead_, &crc);
    Push16(max_entry_lag_, &crc);
    Push16(static_cast<uint16_t>(
        overhead_count_ ? overhead_sum_ / overhead_count_ : 0), &crc);
    EndPacket(crc);
  }

  // Report loop: HELLO, LADDER, every engine result, END, then a gap.
  void EnqueueReportItem() {
    const int items = 3 + PLAITS_OVERRUN_SWEEP_ENGINES;
    if (report_item_ == 0) {
      EnqueueHello();
    } else if (report_item_ == 1) {
      EnqueueLadder();
    } else if (report_item_ < items - 1) {
      EnqueueEngineResult(report_item_ - 2);
    } else {
      EnqueueEnd();
    }
    ++report_item_;
    if (report_item_ >= items) {
      report_item_ = 0;
      report_gap_ = kSampleRateInt;  // one second of silence per pass
    }
  }

  // ---- FSK modulator: UART framing, idle mark around each burst ----

  bool Transmitting() const {
    return fsk_active_ || queue_head_ != queue_tail_;
  }

  short FskSample() {
    if (!fsk_active_) {
      fsk_active_ = true;
      bit_phase_ = 0.0f;
      LoadNextBit();
    }
    tone_phase_ += current_bit_ ? mark_increment_ : space_increment_;
    if (tone_phase_ >= 1.0f) tone_phase_ -= 1.0f;
    const short sample = static_cast<short>(Sine(tone_phase_) * 16000.0f);
    bit_phase_ += bit_increment_;
    if (bit_phase_ >= 1.0f) {
      bit_phase_ -= 1.0f;
      if (!LoadNextBit()) fsk_active_ = false;
    }
    return sample;
  }

  // Loads the bit that starts now; false when the burst is over.
  bool LoadNextBit() {
    if (bits_left_) {
      current_bit_ = shift_ & 1;
      shift_ >>= 1;
      --bits_left_;
      return true;
    }
    if (queue_head_ != queue_tail_) {
      if (lead_in_bits_) {
        // A run of idle mark lets the host settle before the first start bit.
        current_bit_ = 1;
        --lead_in_bits_;
        return true;
      }
      const uint8_t byte = queue_[queue_head_];
      queue_head_ = (queue_head_ + 1) % kQueueSize;
      // start (0), 8 data bits LSB first, stop (1)
      shift_ = (static_cast<uint32_t>(byte) << 1) | (1u << 9);
      current_bit_ = shift_ & 1;
      shift_ >>= 1;
      bits_left_ = 9;
      return true;
    }
    if (trail_bits_) {
      current_bit_ = 1;
      --trail_bits_;
      return true;
    }
    lead_in_bits_ = kLeadInBits;
    trail_bits_ = kTrailBits;
    return false;
  }

  int group_;
  Phase phase_;
  int engine_;
  int state_;
  int block_;
  bool stereo_capable_;
  bool stereo_capable_known_;

  OverrunSweepEngineResult results_[PLAITS_OVERRUN_SWEEP_ENGINES];
  uint16_t ladder_peak_[kLadderSteps];
  uint16_t ladder_late_[kLadderSteps];
  uint16_t ladder_late_with_overhead_[kLadderSteps];
  uint16_t condition_peak_[kConditions];
  uint16_t condition_late_[kConditions];
  uint16_t tail_case_peak_[kTailCases];
  uint16_t tail_case_late_[kTailCases];
  uint16_t tail_case_final_peak_[kTailCases];
  int last_state_late_;
  uint32_t last_late_total_;
  uint32_t last_double_total_;

  float pilot_cos_;
  float pilot_sin_;
  float pilot_x_;
  float pilot_y_;
  float mark_increment_;
  float space_increment_;
  float bit_increment_;
  float tone_phase_;
  float bit_phase_;
  uint8_t queue_[kQueueSize];
  volatile int queue_head_;  // consumer: the interrupt's FSK modulator
  volatile int queue_tail_;  // producer: Poll() (Init and report: interrupt)
  uint32_t shift_;
  int bits_left_;
  int lead_in_bits_;
  int trail_bits_;
  bool fsk_active_;
  int current_bit_;
  int report_item_;
  int report_gap_;

  uint32_t callback_start_;
  uint32_t render_end_;
  float usage_;
  float target_load_;
  uint32_t total_blocks_;
  uint16_t max_overhead_;
  uint16_t max_entry_lag_;
  uint32_t overhead_sum_;
  uint32_t overhead_count_;
  Settings settings_;
  int settings_key_;
  int condition_;
  volatile Request request_;
  volatile int request_engine_;
  bool final_;

  DISALLOW_COPY_AND_ASSIGN(OverrunSweep);
};

}  // namespace plaits

#endif  // PLAITS_OVERRUN_SWEEP_H_
