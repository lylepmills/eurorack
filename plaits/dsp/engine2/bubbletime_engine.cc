// Copyright 2026 Combust.
// SPDX-License-Identifier: MIT

#include "plaits/dsp/engine2/bubbletime_engine.h"

#include "plaits/dsp/oscillator/sine_oscillator.h"

#include "plaits/dsp/engine2/ars_luts.h"

namespace plaits {

using namespace stmlib;

namespace {

// Zones: 0 sigma-delta rigid, 1 jittered lattice, 2 quasiperiodic rotation,
// 3 GUE-like, 4 GOE-like, 5 Poisson (+dead-time TIMBRE), 6 clustered.
const int kNumZones = 7;

const float kModulusLadder[8] = {
  3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 12.0f, 16.0f };

// The gap melody: realized gap size buckets onto the four chord tones of
// the current ChordBank chord (long gaps reach the high voices).
const float kGapThresholds[3] = { 0.7f, 1.05f, 1.6f };

const float kGateLevel = 0.8f;
const int kExciteSamples = 48;  // ~1 ms strike ramp
// Mean event-rate cap ~575 Hz: the fastest articulated roll. Beyond this,
// events fuse into pitch-noise and V/oct stops reading (hardware pass,
// 2026-08-16: "keys only audible in the lower third"). ARS is a percussion
// instrument — sparse Taiko hits to granular rolls, always articulate.
const float kMaxRate = 0.012f;
const float kMinGap = 2.0f;     // samples
const float kMinRingSamples = 1440.0f;  // 30 ms: plucks stay pitched at any rate

inline uint32_t XorShift(uint32_t* state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

inline float ToUniform(uint32_t x) {
  return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

inline float InterpTable(const float* table, float u) {
  float x = u * 255.0f;
  CONSTRAIN(x, 0.0f, 255.0f);
  const int i = static_cast<int>(x);
  const float f = x - static_cast<float>(i);
  const int j = i < 255 ? i + 1 : 255;
  return table[i] + (table[j] - table[i]) * f;
}

// Linear interpolation of quantile functions between k tables — a
// Wasserstein-2 geodesic between the gap distributions.
inline float GammaGap(float u, float k) {
  CONSTRAIN(k, 0.5f, 3.0f);
  int t;
  float blend;
  if (k <= 1.0f) {
    t = 0;
    blend = (k - 0.5f) * 2.0f;
  } else if (k <= 2.0f) {
    t = 1;
    blend = k - 1.0f;
  } else {
    t = 2;
    blend = (k - 2.0f);
  }
  const float a = InterpTable(ars::kGammaQuantiles[t], u);
  const float b = InterpTable(ars::kGammaQuantiles[t + 1], u);
  return a + (b - a) * blend;
}

inline float ZoneK(int zone, float timbre) {
  switch (zone) {
    case 3: return 2.2f + 0.8f * timbre;   // GUE-like surmise, k ~ 3
    case 4: return 1.6f + 0.8f * timbre;   // GOE-like surmise, k ~ 2
    case 5: return 1.0f;                   // Poisson (TIMBRE = dead time)
    default: return 1.0f - 0.55f * timbre; // clustered, k < 1
  }
}

// TIMBRE descends the Stern-Brocot tree from the 1/2 root in a straight
// shot toward x — the fractional part of the chord's top tone — passing
// through its mediant ancestors: every stop is that interval's next-best
// rational approximation, so the chord table defines the rhythmic locks.
// With the library's Golden Ratio (833) chord table the target is 1/phi and
// the original noble-path behavior is recovered as a special case.
inline float BrocotDescentAlpha(float x, float timbre) {
  CONSTRAIN(x, 0.02f, 0.98f);
  const float depth = timbre * 14.0f;
  const int levels = static_cast<int>(depth);
  const float blend = depth - static_cast<float>(levels);
  int32_t a = 0, b = 1, c = 1, d = 1;
  float previous = 0.5f;
  float current = 0.5f;
  for (int i = 0; i <= levels; ++i) {
    const int32_t num = a + c;
    const int32_t den = b + d;
    const float mediant =
        static_cast<float>(num) / static_cast<float>(den);
    previous = current;
    current = mediant;
    if (x < mediant) {
      c = num;
      d = den;
    } else {
      a = num;
      b = den;
    }
  }
  return previous + (current - previous) * blend;
}

}  // namespace

void BubbleTimeEngine::Init(stmlib::BufferAllocator* allocator) {
  chords_.Init(allocator);
  chords_.Reset();
  seed_ = 0x1DEA5EEDu;
  zone_ = 5;
  modulus_index_ = 0;
  base_interval_ = 2;  // unison
  cached_zone_ = -1;
  cached_timbre_ = -1.0f;
  regen_pending_ = 0;
  normed_loop_length_ = -1;
  loop_norm_ = 1.0f;
  hold_blocks_ = 0;
  reseed_armed_ = true;
  dc_aux_ = 0.0f;
  Reseed();
  Reset();
}

// A strike restarts the NECKLACE — playback position, scheduling, wrap —
// but never the resonators: they are the drumhead, and zeroing them
// mid-ring is a hard discontinuity (hardware pass, 2026-08-26: pops on
// note presses, and the Plaits LPG has no attack to hide them). The old
// ring decays naturally under the new hits.
void BubbleTimeEngine::RestartPlayback() {
  gap_pos_ = 0;
  event_countdown_ = 1.0f;
  cumulative_position_ = 0.0f;
  cumulative_position_b_ = 0.0f;
  samples_since_event_ = 0.0f;
  last_lattice_u_ = 0.5f;
  gate_remaining_ = 0;
  gate_level_ = kGateLevel;
  sd_accumulator_ = 1.0f;  // fire on the first sample: a strike is an event
  rotation_phase_ = 0.0f;
  rotation_countdown_ = 1.0f;
}

void BubbleTimeEngine::Reset() {
  RestartPlayback();
  a_re_ = a_im_ = 0.0f;
  b_re_ = b_im_ = 0.0f;
  a_right_re_ = a_right_im_ = 0.0f;
  b_right_re_ = b_right_im_ = 0.0f;
  a_c_ = b_c_ = 1.0f;
  a_s_ = b_s_ = 0.0f;
  resonator_r_ = 0.99f;
  excite_remaining_ = 0;
  excite_amp_ = 0.0f;
  excite_left_ = excite_right_ = 0.70710678f;
}

void BubbleTimeEngine::RefillUniforms() {
  seed_ = seed_ * 1664525u + 1013904223u;
  uint32_t state = seed_ ? seed_ : 1u;
  for (int i = 0; i < kLoopGaps; ++i) {
    uniforms_[i] = ToUniform(XorShift(&state));
  }
  freerun_state_ = state;
}

void BubbleTimeEngine::Reseed() {
  // Init-time path only: full synchronous regeneration follows via the
  // invalidated cache. The live reseed gesture uses RefillUniforms +
  // StartIncrementalRegen instead — a mid-note seed must not cost a whole
  // ring of LUT work in one audio block (hardware pass, 2026-08-16: CPU
  // spike ~1.5 s into every held note).
  RefillUniforms();
  cached_zone_ = -1;
  ring_valid_ = false;
}

void BubbleTimeEngine::RegenerateGaps(int zone, float timbre) {
  // Same uniforms, new transform: the necklace's identity survives
  // re-statisticization across the ladder. Full pass — init/reseed only.
  regen_k_ = ZoneK(zone, timbre);
  for (int i = 0; i < kLoopGaps; ++i) {
    gap_ring_[i] = GammaGap(uniforms_[i], regen_k_);
  }
  regen_pending_ = 0;
  normed_loop_length_ = -1;
  cached_zone_ = zone;
  cached_timbre_ = timbre;
}

void BubbleTimeEngine::StartIncrementalRegen(int zone, float timbre) {
  // Knob-driven changes spread the regeneration across blocks (8 gaps per
  // block) so turning HARMONICS or TIMBRE cannot spike the audio callback.
  regen_k_ = ZoneK(zone, timbre);
  regen_pending_ = kLoopGaps;
  regen_write_ = 0;
  cached_zone_ = zone;
  cached_timbre_ = timbre;
}

void BubbleTimeEngine::UpdateLoopNorm() {
  // Normalize the loop to exact clock time: the first L gaps sum to L, so a
  // TRIG-replayed realization phrase-locks to a bar. Conditioning on the sum
  // barely dents the spacing statistics.
  float sum = 0.0f;
  for (int i = 0; i < loop_length_; ++i) {
    sum += gap_ring_[i];
  }
  loop_norm_ = sum > 0.0f ? static_cast<float>(loop_length_) / sum : 1.0f;
  normed_loop_length_ = loop_length_;
}

// Next gap in mean-gap units, for the stochastic zones (1, 3-6).
float BubbleTimeEngine::NextGap(int zone, float timbre,
                                 float mean_gap_samples) {
  // Fraying: with probability fray_, this gap is a fresh ensemble draw
  // instead of the stored one. fray_ = 1 is exact FREERUN.
  const bool fresh = fray_ > 0.0f &&
      ToUniform(XorShift(&freerun_state_)) < fray_;
  float gap;
  if (zone == 1) {
    // Jittered lattice: bounded displacement, truly hyperuniform. Gaps are
    // differences of successive point displacements, so a fresh draw chains
    // from the previous uniform rather than drawing two.
    const float jitter = 0.9f * timbre;
    float u0;
    float u1;
    if (fresh) {
      u0 = last_lattice_u_;
      u1 = ToUniform(XorShift(&freerun_state_));
    } else {
      u0 = uniforms_[gap_pos_ % loop_length_];
      u1 = uniforms_[(gap_pos_ + 1) % loop_length_];
    }
    last_lattice_u_ = u1;
    gap = 1.0f + jitter * (u1 - u0);
  } else if (fresh) {
    gap = GammaGap(ToUniform(XorShift(&freerun_state_)),
                   ZoneK(zone, timbre));
  } else {
    gap = gap_ring_[gap_pos_ % loop_length_] * loop_norm_;
  }
  if (zone == 5) {
    // Poisson dead time: TIMBRE inserts a refractory floor (hard-core
    // process), mean-normalized so rate stays put. An honest, audible
    // evenness control for the otherwise parameter-free null.
    const float floor_amount = timbre * 0.6f;
    gap = (gap + floor_amount) / (1.0f + floor_amount);
  }
  ++gap_pos_;
  float samples = gap * mean_gap_samples;
  if (samples < kMinGap) {
    samples = kMinGap;
  }
  return samples;
}

void BubbleTimeEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;
  const bool stereo = PLAITS_STEREO_BUBBLETIME && parameters.stereo;
  if (!stereo) {
    // Do not leave a frozen stereo tail waiting behind the ordinary AUX-gate
    // mode. Re-entering stereo starts the right-hand resonators cleanly while
    // the original mono path remains byte-for-byte the same.
    a_right_re_ = a_right_im_ = 0.0f;
    b_right_re_ = b_right_im_ = 0.0f;
  }

  // Rigidity ladder with hysteresis.
  const float raw_zone = parameters.harmonics * static_cast<float>(kNumZones);
  int candidate = static_cast<int>(raw_zone);
  CONSTRAIN(candidate, 0, kNumZones - 1);
  if (candidate != zone_ &&
      (raw_zone > static_cast<float>(zone_) + 1.08f ||
       raw_zone < static_cast<float>(zone_) - 0.08f)) {
    zone_ = candidate;
  }

  // macro: CCW tightens the loop to 8 gaps; noon is the stock 64; CW frays
  // the loop — fresh ensemble draws with rising probability, FREERUN at the
  // end. No dead zones.
  const float macro = parameters.macro;
  fray_ = macro > 0.5f ? (macro - 0.5f) * 2.0f : 0.0f;
  loop_length_ = macro < 0.5f
      ? 8 + static_cast<int>(macro * 2.0f * 56.0f)
      : kLoopGaps;

  // TIMBRE splits: lower half is necklace tempo — quarter speed at full CCW
  // rising to full speed at noon, pitch untouched (hardware pass,
  // 2026-08-26: "we need some way of slowing it down"). Upper half is the
  // in-zone character. Tempo moves never regenerate the statistics ring.
  const float timbre = parameters.timbre;
  const float character = timbre > 0.5f ? (timbre - 0.5f) * 2.0f : 0.0f;
  const float tempo = timbre < 0.5f ? 0.25f + timbre * 2.0f * 0.75f : 1.0f;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    RestartPlayback();
    hold_blocks_ = 0;
    reseed_armed_ = true;
  }
  if (parameters.trigger & TRIGGER_HIGH) {
    ++hold_blocks_;
    if (reseed_armed_ && hold_blocks_ > 6000) {
      RefillUniforms();
      if (ring_valid_) {
        StartIncrementalRegen(zone_, character);
      }
      reseed_armed_ = false;
    }
  } else {
    hold_blocks_ = 0;
  }

  if (zone_ != cached_zone_ ||
      (zone_ >= 3 && (cached_timbre_ - character > 0.008f ||
                      character - cached_timbre_ > 0.008f))) {
    if (ring_valid_) {
      StartIncrementalRegen(zone_, character);
    } else {
      RegenerateGaps(zone_, character);
      ring_valid_ = true;
    }
  }
  if (regen_pending_ > 0) {
    const int chunk = regen_pending_ < 8 ? regen_pending_ : 8;
    for (int i = 0; i < chunk; ++i) {
      gap_ring_[regen_write_] = GammaGap(uniforms_[regen_write_], regen_k_);
      ++regen_write_;
    }
    regen_pending_ -= chunk;
    if (regen_pending_ == 0) {
      normed_loop_length_ = -1;
    }
  }
  if (normed_loop_length_ != loop_length_ && regen_pending_ == 0) {
    UpdateLoopNorm();
  }

  float f0 = NoteToFrequency(parameters.note);
  CONSTRAIN(f0, 1e-6f, kMaxRate);

  // Wrap modulus, hysteresis-quantized (a CV on a boundary must not chatter
  // the necklace). Lower MORPH half only; upper half is the ring control.
  const float morph = parameters.morph;
  float morph_low = morph < 0.5f ? morph * 2.0f : 1.0f;
  const float raw_m = morph_low * 7.99f;
  int m_candidate = static_cast<int>(raw_m);
  CONSTRAIN(m_candidate, 0, 7);
  if (m_candidate != modulus_index_ &&
      (raw_m > static_cast<float>(modulus_index_) + 1.08f ||
       raw_m < static_cast<float>(modulus_index_) - 0.08f)) {
    modulus_index_ = m_candidate;
  }
  const float modulus = kModulusLadder[modulus_index_];
  // Second wrap ring for the AUX booleans: the next modulus up the ladder,
  // so MORPH-low sweeps polymeter pairs (3:4, 4:5, ... 12:16, 16:3).
  const float modulus_b = kModulusLadder[(modulus_index_ + 1) & 7];
  const float ring_amount = morph > 0.5f ? (morph - 0.5f) * 2.0f : 0.0f;

  // Each modulus brings its chord: pattern and harmony ride one knob, and
  // Palette chord tables (chord_set_option) apply.
  chords_.set_chord(static_cast<float>(modulus_index_) * (1.0f / 7.0f),
                    parameters.chord_set_option);
  const float* chord_ratios = chords_.ratios();

  // Percussion rate law (hardware passes, 2026-08-16: "keys don't register
  // above C1" — the old m x f0 density multiplier, a relic of the deleted
  // contour renderer, fused everything above the crossfade). Pluck pitch
  // tracks V/oct fully; event density saturates hyperbolically toward a
  // ~120 Hz roll ceiling — dense enough to roll, sparse enough that every
  // pluck keeps its pitch. One continuous law, no crossfade cliff.
  const float f0_hz = f0 * kSampleRate;
  // Roll ceiling 30 Hz: a fast roll, never a buzz. Note-pinning measurement
  // (2026-08-16) showed a 120 Hz ceiling put the roll's own repetition into
  // pitch range — its comb masked the pluck pitch and the ear tracked the
  // compressed rate law instead of V/oct.
  const float rate_hz = tempo * (f0_hz / (1.0f + f0_hz * (1.0f / 30.0f)));
  float rate = rate_hz * (1.0f / kSampleRate);
  CONSTRAIN(rate, 1e-6f, kMaxRate);
  const float mean_gap = 1.0f / rate;

  // Gate DC handling keyed on the actual event rate: unipolar patch-ready
  // gates at CV rates, AC-coupled at roll rates.
  float density_blend = (rate_hz - 40.0f) * (1.0f / 40.0f);
  CONSTRAIN(density_blend, 0.0f, 1.0f);
  density_blend = density_blend * density_blend * (3.0f - 2.0f * density_blend);

  // Pluck pitch tracks V/oct absolutely; only sub-55 Hz notes fold up by
  // octaves so CV-rate/LOW-range playing stays audible. The fold-down
  // ceiling of earlier revisions is gone (hardware pass, 2026-08-17: with
  // the pitch-range dial in play, ascending lines must not wrap).
  float f_pitch = NoteToFrequency(parameters.note);
  while (f_pitch < 0.001146f) {
    f_pitch += f_pitch;
  }

  // Resonator decay: ~3.5 mean gaps with an absolute 30 ms floor so fast
  // rolls ring pitched instead of clicking.
  float tau = 3.5f * mean_gap;
  if (tau < kMinRingSamples) {
    tau = kMinRingSamples;
  }
  float r = 1.0f - 1.0f / tau;
  CONSTRAIN(r, 0.90f, 0.99995f);
  resonator_r_ = r;
  // When rings overlap many events, scale injection down so dense rolls
  // don't pile up into the limiter.
  float injection = 1.7f * Sqrt(mean_gap / tau);
  if (injection > 1.0f) {
    injection = 1.0f;
  }

  // Resonator B is the BASE TONE: it never retunes between events, so its
  // phase-coherent re-strikes fuse into a sustained drone under the percs
  // (Will's hearing, 2026-08-16 — made intentional). Upper MORPH sets its
  // interval against the played tone, hysteresis-quantized.
  static const float kBaseIntervals[5] = {
    -12.0f, -5.0f, 0.0f, 7.0f, 12.0f };
  const float raw_interval = ring_amount * 4.99f;
  int interval_candidate = static_cast<int>(raw_interval);
  CONSTRAIN(interval_candidate, 0, 4);
  if (interval_candidate != base_interval_ &&
      (raw_interval > static_cast<float>(base_interval_) + 1.08f ||
       raw_interval < static_cast<float>(base_interval_) - 0.08f)) {
    base_interval_ = interval_candidate;
  }
  float b_freq = f_pitch * SemitonesToRatio(kBaseIntervals[base_interval_]);
  CONSTRAIN(b_freq, 0.0f, 0.45f);
  b_c_ = r * Sine(b_freq + 0.25f);
  b_s_ = r * Sine(b_freq);
  const float b_gain = 0.42f;

  // Zone 0 sigma-delta leak is rate-proportional so TIMBRE is a character
  // control at every pitch instead of an event-killer at LFO rates.
  const float leak = zone_ == 0 ? character * rate * 0.3f : 0.0f;
  float chord_fraction = chord_ratios[3];
  chord_fraction -= static_cast<float>(static_cast<int>(chord_fraction));
  const float alpha = BrocotDescentAlpha(chord_fraction, character);
  const float rotation_step = mean_gap * alpha;

  const int gate_length = static_cast<int>(
      mean_gap * 0.35f > 120.0f ? 120.0f : mean_gap * 0.35f) + 4;

  for (size_t i = 0; i < size; ++i) {
    bool event = false;
    samples_since_event_ += 1.0f;
    if (zone_ == 0) {
      sd_accumulator_ += rate;
      sd_accumulator_ -= leak * sd_accumulator_;
      if (sd_accumulator_ >= 1.0f) {
        sd_accumulator_ -= 1.0f;
        event = true;
      }
    } else if (zone_ == 2) {
      rotation_countdown_ -= 1.0f;
      if (rotation_countdown_ <= 0.0f) {
        rotation_countdown_ += rotation_step;
        rotation_phase_ += alpha;
        if (rotation_phase_ >= 1.0f) {
          rotation_phase_ -= 1.0f;
        }
        if (rotation_phase_ < alpha) {
          event = true;  // three-distance return times, literally
        }
      }
    } else {
      event_countdown_ -= 1.0f;
      if (event_countdown_ <= 0.0f) {
        event_countdown_ += NextGap(zone_, character, mean_gap);
        event = true;
      }
    }

    if (event) {
      const float gap_units = samples_since_event_ / mean_gap;
      samples_since_event_ = 0.0f;

      // Wrap position; crossing the modulus is the necklace downbeat.
      cumulative_position_ += gap_units;
      bool downbeat = false;
      if (cumulative_position_ >= modulus) {
        cumulative_position_ -=
            modulus * static_cast<float>(
                static_cast<int>(cumulative_position_ / modulus));
        downbeat = true;
      }
      cumulative_position_b_ += gap_units;
      if (cumulative_position_b_ >= modulus_b) {
        cumulative_position_b_ -=
            modulus_b * static_cast<float>(
                static_cast<int>(cumulative_position_b_ / modulus_b));
      }

      // Velocity: long gaps accent, downbeats accent hardest.
      float vel = 0.55f + 0.45f * (gap_units - 0.4f) * 0.8f;
      CONSTRAIN(vel, 0.25f, 1.0f);
      if (downbeat) {
        vel = 1.0f;
      }

      // Necklace booleans (design doc 8.4): the same point set wrapped onto
      // two rings, XOR of their half-windows = interference rhythm. Every
      // event still emits (statistics stay measurable; low-threshold inputs
      // hear all of it) — the boolean sets a two-tier accent, so ordinary
      // trigger thresholds hear only the polymetric pattern.
      const bool ring1 = cumulative_position_ < 0.5f * modulus;
      const bool ring2 = cumulative_position_b_ < 0.5f * modulus_b;
      const float boolean_tier = ring1 != ring2 ? 1.0f : 0.5f;

      // In stereo mode the same two wrap rings become a deterministic spatial
      // score. Their continuous phases place each bubble; the XOR still drives
      // accents, so the stereo motion and the ordinary AUX rhythm are two views
      // of the same necklace rather than unrelated decoration.
      if (stereo) {
        const float phase_a = cumulative_position_ / modulus;
        const float phase_b = cumulative_position_b_ / modulus_b;
        float pan = 0.5f + 0.34f * Sine(phase_a) +
            0.12f * Sine(phase_b + 0.25f);
        CONSTRAIN(pan, 0.04f, 0.96f);
        StereoPanGains(pan, &excite_left_, &excite_right_);
      }

      // The gap melody: bucket the realized gap onto a chord tone and
      // retune resonator A. Rigid zones give ostinati; the three-distance
      // zone gives a three-note motif; Poisson gives free melody.
      int bucket = 0;
      while (bucket < 3 && gap_units > kGapThresholds[bucket]) {
        ++bucket;
      }
      float a_freq = f_pitch * chord_ratios[bucket];
      CONSTRAIN(a_freq, 0.0f, 0.45f);
      a_c_ = resonator_r_ * Sine(a_freq + 0.25f);
      a_s_ = resonator_r_ * Sine(a_freq);

      // Strike via a ~1 ms raised-cosine ramp instead of a one-sample step:
      // the attack stays drum-tight but the wideband click on OUT is gone
      // (hardware pass, 2026-08-16). AUX keeps its fast edges — those are
      // correct for CV.
      excite_amp_ = vel * injection;
      excite_remaining_ = kExciteSamples;
      gate_remaining_ = gate_length;
      gate_level_ = kGateLevel * boolean_tier * (0.75f + 0.25f * vel);
    }

    if (excite_remaining_ > 0) {
      const float p =
          static_cast<float>(kExciteSamples - excite_remaining_ + 1) *
          (1.0f / static_cast<float>(kExciteSamples));
      // Hann increments summing to 1: (1 - cos(2 pi p)) / N.
      const float w = (1.0f - Sine(p + 0.25f)) *
          (1.0f / static_cast<float>(kExciteSamples));
      const float excite = excite_amp_ * w;
      if (stereo) {
        a_re_ += excite * excite_left_;
        a_right_re_ += excite * excite_right_;
        // Keep the base-tone resonator centered so the moving gap melody has
        // an acoustic anchor and neither channel becomes bass-light.
        const float centered = excite * 0.70710678f;
        b_re_ += centered;
        b_right_re_ += centered;
      } else {
        a_re_ += excite;
        b_re_ += excite;
      }
      --excite_remaining_;
    }

    // Ping resonators: complex one-pole rotations.
    float t = a_re_;
    a_re_ = a_c_ * a_re_ - a_s_ * a_im_;
    a_im_ = a_s_ * t + a_c_ * a_im_;
    t = b_re_;
    b_re_ = b_c_ * b_re_ - b_s_ * b_im_;
    b_im_ = b_s_ * t + b_c_ * b_im_;
    if (stereo) {
      t = a_right_re_;
      a_right_re_ = a_c_ * a_right_re_ - a_s_ * a_right_im_;
      a_right_im_ = a_s_ * t + a_c_ * a_right_im_;
      t = b_right_re_;
      b_right_re_ = b_c_ * b_right_re_ - b_s_ * b_right_im_;
      b_right_im_ = b_s_ * t + b_c_ * b_right_im_;
      out[i] = SoftLimit(0.6f * (a_re_ + b_gain * b_re_));
      aux[i] = SoftLimit(
          0.6f * (a_right_re_ + b_gain * b_right_re_));
    } else {
      out[i] = SoftLimit(0.6f * (a_re_ + b_gain * b_re_));
    }

    // Unipolar patch-ready gates at CV rates; DC removal crossfades in with
    // density at audio rates, where the gate stream is a high-duty train.
    const float g = gate_remaining_ > 0 ? gate_level_ : 0.0f;
    if (gate_remaining_ > 0) {
      --gate_remaining_;
    }
    dc_aux_ += 0.002f * (g - dc_aux_);
    if (!stereo) {
      aux[i] = g - density_blend * dc_aux_;
    }
  }
}

}  // namespace plaits
