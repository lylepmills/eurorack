// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' QUESTION_MARK: a Morse-code transmitter keying a sine over a
// wandering noise bed, through a squared-distortion stage.

#include "plaits/dsp/engine2/question_mark_engine.h"

#include "stmlib/dsp/dsp.h"

namespace plaits {

using namespace stmlib;

namespace {

// Braids' wt_code (braids/resources.cc:11157), verbatim: 1064 bytes of
// packed 2-bit symbols, LOW pair first within each byte. Extracted
// programmatically, never retyped, and checked three ways (see
// question_mark_engine.h TABLES):
//
//   * byte-identical to the literal `random_data` list its generator
//     braids/resources/waveforms.py:157-227 appends as ('code', ...) --
//     0 of 1064 bytes differ. There is no closed form to reproduce; the
//     generator simply carries the data.
//   * repacking the decoded symbol stream reproduces all 1064 bytes.
//   * all 4252 symbols before the terminator decode to well-formed
//     International Morse -- 939 characters in 158 words, zero unrecognised
//     patterns. A wrong bit order or a wrong shift would not survive that.
//
// The message is the "Scope" bar passage from Thomas Pynchon's THE CRYING OF
// LOT 49, which is what the module's "49" marquee unlock refers to. It is
// carried as data, exactly as the module carries it, and is not written out
// in text anywhere in this port.

const uint8_t kMorseCode[1064] = {
     5,    0,  132,    0,   20,   16,   20,   81,   16,   65,    8,   17,
     4,   65,   17,    5,    0,   69,    1,   88,   17,   25,    0,  132,
   144,    0,   64,   80,    0,   21,  148,    0,   65,   17,    5,  129,
     4,    1,   68,    1,   65,    5,   65,   17,   21,    4,   20,   16,
     4,  128,   80,    0,    4,   64,   20,   21,    0,    4,    1,   20,
    16,    9,   17,   68,   17,    5,   17,    4,    1,  132,    0,   65,
    16,   20,   81,  145,    1,   81,   17,   21,   16,   21,   81,    1,
    20,   16,   21,   68,   16,   16,  144,    5,    0,  132,   17,    4,
    65,   68,  129,   65,   20,   81,  129,    0,    4,   20,   65,  129,
    17,    5,   80,    5,   64,   64,    1,  132,   64,   65,   17,   68,
    65,   64,   17,    5,   80,    0,    4,    1,   20,   16,   25,   64,
     4,   17,    4,   20,   16,   84,   20,  128,    5,    0,  132,    0,
    65,   80,    1,   65,   24,   81,    0,  129,   80,  129,   17,    5,
    64,    1,   65,   64,   16,   64,   16,   64,   17,    5,   64,    1,
    65,   17,    5,   17,  132,    5,   80,    1,   64,   20,    1,   16,
    25,   81,    0,   80,    4,  129,   16,    5,    0,    4,   20,   16,
     4,    0,    8,   16,    4,    1,   20,   81,   16,   69,    1,    8,
    21,   72,    0,   80,    4,   65,    1,    0,   80,    1,   65,    0,
    64,   80,   65,   17,    4,   20,   20,  129,    1,    4,   21,   20,
    16,  132,   17,    5,   16,    8,    1,    4,    4,   64,    0,    4,
     5,   17,   21,   81,    0,    5,  128,    0,   65,   64,   17,    4,
    80,   16,   68,    0,    4,    1,    1,   89,   17,   21,   64,   80,
     1,  145,    0,   68,   20,   69,    1,   88,   17,   25,    0,  132,
    16,   65,   80,    1,   81,    1,    4,   21,   16,   21,    1,   16,
     9,   21,   20,  128,    4,    0,   69,   16,   20,   16,   21,   81,
    65,    8,   20,    4,   64,   64,    1,  132,   21,  145,    1,   64,
    24,   64,   16,    4,   80,   65,    1,    9,    5,    1,    4,   65,
     4,   21,   64,    1,   81,   16,   16,  144,   17,    4,    1,    4,
     5,   65,   20,  128,   80,    9,    5,    1,    4,    1,   88,    0,
    64,    8,   81,   80,    1,   81,   17,    0,  145,   17,   69,    1,
     4,    4,   17,    4,    9,   21,   20,  128,   81,   84,   17,   64,
    17,   68,   16,    8,   16,   20,   81,    0,   21,   20,  128,  144,
     5,   21,    0,    4,    1,  132,   64,  129,    1,   64,   80,    1,
    65,    1,    5,    1,    1,    9,    9,   81,   64,   17,   64,   20,
    68,    1,   24,    0,    4,    5,   65,   69,   65,    1,   68,   16,
     8,   81,    4,    5,   65,   64,   65,   17,    8,   64,    0,   64,
    80,    1,   68,    0,   24,    5,   85,    4,   65,   64,   80,   16,
    64,   64,   17,   64,   20,  128,   80,   65,    1,   24,   69,   21,
     1,   20,   65,    4,  129,   17,    5,   65,    1,   68,   16,   68,
     1,   24,    0,   20,   81,    0,    5,   65,    1,   64,   17,   21,
     4,    1,    1,   25,   81,   20,   64,   64,   16,   65,   80,   17,
     0,  145,    1,   65,    0,   64,   20,   16,   20,   80,   64,   65,
    17,   88,    0,   64,   24,    0,    4,    5,   65,   17,    0,  145,
    17,    4,   65,    4,  145,   65,    4,   65,    0,   80,   17,    5,
    80,    0,    4,    1,    1,   89,    0,   64,    4,   65,    8,   81,
    80,    0,   88,    0,   64,   64,    0,    8,   17,  133,   65,    8,
    64,   80,   64,    0,   24,    1,    5,   80,   17,    5,    8,   21,
     0,   20,   81,    0,  149,    5,    0,  132,    0,   20,   16,   20,
    81,   16,   65,   24,   16,    4,   65,   17,    5,   81,    1,   20,
    17,    0,   88,    0,   64,   20,   16,    9,    5,    1,    4,    1,
     8,   81,   17,    5,   65,   24,   65,   16,   64,   80,    0,    4,
    64,    4,  128,   80,   65,    1,    8,   64,    5,    5,   65,   20,
   128,   80,   25,   16,   21,   81,    0,   21,    1,   16,    9,   64,
    64,   16,   64,   20,   84,   16,   16,  144,   20,    0,   21,   16,
    68,   16,   65,    9,   16,   20,   81,   16,    8,   25,   16,   20,
    81,    0,    5,   17,    4,    1,   68,    1,    0,   80,    5,    0,
     4,   65,  132,   65,    4,    5,   65,    4,  129,    5,    0,  132,
     1,   20,   81,   17,    5,   65,   17,    0,  145,   16,    5,    0,
    20,  145,   16,   69,   16,  132,   20,   20,   65,   80,   17,   68,
     1,    8,   20,    8,   25,   20,   81,    0,   68,    1,    0,   80,
    16,   65,   64,    1,   65,    1,    5,   20,   20,  129,    1,   65,
    17,   21,   84,    4,   64,   21,    1,   16,    9,   64,   68,   64,
    65,   17,    8,    0,   20,   81,   16,    9,   16,    4,    5,  129,
     5,    0,   68,    1,  145,    1,   65,   17,    5,   80,   16,   64,
     1,    8,   16,    4,    1,    4,   20,   16,   20,  144,   64,    9,
    21,   16,    4,   65,   17,    5,   64,    0,   88,    0,   64,    8,
    65,   17,   21,   81,   81,   16,   16,  144,  144,    0,    4,   80,
     1,   20,   64,   20,   24,   16,    4,    0,   20,   81,   16,    4,
    80,    0,   24,   81,    0,  129,   16,    5,    0,   20,   81,   17,
     5,   17,    4,  128,   80,   65,    1,   24,   16,    5,   20,    0,
    20,    0,    4,    1,   68,    0,   24,    0,    4,   80,   16,    4,
    64,    9,   16,    4,   65,   17,   21,    9,   25,   80,   64,   65,
     1,   24,   81,    0,  129,   16,   81,    0,   21,   80,   24,    0,
    20,   81,    1,  144,   80,   89,    0,   64,    8,   16,    4,    5,
   129,   20,   20,  128,   17,    5,   16,   88,    0,   64,    8,   65,
    17,   21,   81,   81,   16,   16,  144,    4,    0,   69,   16,   20,
    16,   21,    0,   20,   81,    1,   20,   16,   25,    1,    5,   80,
    64,   89,   80,   16,   64,    1,    5,   20,   20,   65,   16,   16,
   144,    5,    0,  132,    1,   64,   80,   16,   84,   20,   20,   64,
     4,  129,    5,    4,   17,   84,   17,   69,    1,   24,    0,    4,
    21,   16,   20,   80,   17,    0,  145,   16,    5,   84,    0,  128,
     5,    0,  132,    1,    4,   65,   64,   65,    1,    5,   64,   16,
    16,  144,   16,    5,    0,    4,   85,   16,   17,   65,    0,    8,
     0,    4,    5,   17,    4,   17,   68,   65,   64,   65,   17,    4,
    16,    1,   24,   81,   20,   64,   64,   16,   65,  144,   16,    5,
     0,    4,    4,   64,   16,   65,    4,   65,   20,   64,   16,   16,
   144,    5,    0,    4,   85,   16,   17,   65,    0,   24,    0,   20,
    16,    9,   64,   21,   81,    1,   65,    1,    5,    0,    4,    5,
    80,    0,   68,   65,   16,   16,   80,  255
};

// Braids' wav_sine (braids/resources.cc:1461), verbatim, 257 int16.
//
// Reproduced from braids/resources/waveforms.py:97,117 --
// scale(sine[quadrature]) over -cos(2*pi*x) at 257 points, mean-centred,
// rescaled to +/-32766 and 2nd-order dithered -- to a maximum deviation of
// 1.68 LSB (mean 0.54), which
// is the dither. Against a plain -cos(2*pi*x)*32766 the deviation is 254.6
// LSB: `scale` centres on a 257-point mean that includes a duplicated
// endpoint, which is why entry 0 is -32512 rather than -32766 and why the
// table is not symmetric. That asymmetry is in Braids' signal, so it is in
// this port's.
//
// Embedded rather than computed for two reasons. It disposes of the
// -cos-versus-sin trap fold_engine.h carries a phase offset for; and
// digital_oscillator.cc:2292 reads it TRUNCATED and unsmoothed
// (`wav_sine[(phase >> 22) & 0xff]`), which a closed form cannot reproduce
// without the table's own dither and asymmetry.

const int16_t kBraidsSine[257] = {
   -32512,  -32502,  -32473,  -32423,  -32356,  -32265,  -32160,  -32031,
   -31885,  -31719,  -31533,  -31331,  -31106,  -30864,  -30605,  -30324,
   -30028,  -29712,  -29379,  -29026,  -28658,  -28272,  -27868,  -27449,
   -27011,  -26558,  -26089,  -25604,  -25103,  -24588,  -24056,  -23512,
   -22953,  -22378,  -21793,  -21191,  -20579,  -19954,  -19316,  -18667,
   -18006,  -17334,  -16654,  -15960,  -15259,  -14548,  -13828,  -13100,
   -12363,  -11620,  -10868,  -10112,   -9347,   -8578,   -7805,   -7023,
    -6241,   -5453,   -4662,   -3868,   -3073,   -2274,   -1474,    -674,
      126,     929,    1729,    2527,    3326,    4123,    4916,    5707,
     6495,    7278,    8057,    8833,    9601,   10366,   11122,   11874,
    12618,   13353,   14082,   14802,   15512,   16216,   16906,   17589,
    18260,   18922,   19569,   20207,   20834,   21446,   22045,   22634,
    23206,   23765,   24311,   24842,   25357,   25858,   26343,   26812,
    27266,   27701,   28123,   28526,   28912,   29281,   29632,   29966,
    30281,   30579,   30859,   31118,   31361,   31583,   31788,   31973,
    32139,   32286,   32412,   32521,   32608,   32679,   32725,   32757,
    32766,   32757,   32725,   32679,   32608,   32521,   32412,   32286,
    32139,   31973,   31788,   31583,   31361,   31118,   30859,   30579,
    30281,   29966,   29632,   29281,   28912,   28526,   28123,   27701,
    27266,   26812,   26343,   25858,   25357,   24842,   24311,   23765,
    23206,   22634,   22045,   21446,   20834,   20207,   19569,   18922,
    18260,   17589,   16906,   16216,   15512,   14802,   14082,   13353,
    12618,   11874,   11122,   10366,    9601,    8833,    8057,    7278,
     6495,    5707,    4916,    4123,    3326,    2527,    1729,     929,
      126,    -674,   -1474,   -2274,   -3073,   -3868,   -4662,   -5453,
    -6241,   -7023,   -7805,   -8578,   -9347,  -10112,  -10868,  -11620,
   -12363,  -13100,  -13828,  -14548,  -15259,  -15960,  -16654,  -17334,
   -18006,  -18667,  -19316,  -19954,  -20579,  -21191,  -21793,  -22378,
   -22953,  -23512,  -24056,  -24588,  -25103,  -25604,  -26089,  -26558,
   -27011,  -27449,  -27868,  -28272,  -28658,  -29026,  -29379,  -29712,
   -30028,  -30324,  -30605,  -30864,  -31106,  -31331,  -31533,  -31719,
   -31885,  -32031,  -32160,  -32265,  -32356,  -32423,  -32473,  -32502,
   -32512
};

// stmlib::Random's LCG, stepped per instance rather than from the global
// state the module shares between models. Identical arithmetic: the top 16
// bits of the word, read as a signed 16-bit value.
inline int32_t NextNoiseSample(uint32_t* state) {
  *state = *state * 1664525u + 1013904223u;
  return static_cast<int32_t>(static_cast<int16_t>(*state >> 16));
}

// Braids' pitch-free reads of wav_sine, both preserved exactly.
//
// digital_oscillator.cc:2258, the keyed tone: an 8-bit index with the next
// entry interpolated. This is stmlib::Interpolate824 (stmlib/utils/dsp.h:94-98)
// written out rather than included, including its truncation of the phase
// word's 24-bit fraction to 16 bits -- the arithmetic is Braids' exactly, and
// the 16-bit form is also the only one that fits: the table's steepest step is
// 803, and 803 * 0xffffff overflows int32 where 803 * 0xffff does not.
inline int32_t InterpolateSine(uint32_t phase) {
  const int32_t a = kBraidsSine[phase >> 24];
  const int32_t b = kBraidsSine[(phase >> 24) + 1];
  const int32_t fraction = static_cast<int32_t>((phase >> 8) & 0xffff);
  return a + ((b - a) * fraction >> 16);
}

// digital_oscillator.cc:2292, the noise's ring modulator: a TRUNCATED read
// with no interpolation. `phase >> 22` spans 0..1023 across one cycle of the
// played note and `& 0xff` folds it to 0..255, so this steps through the whole
// table FOUR times per cycle -- the bed is ring-modulated at 4*f0, as a
// 256-step staircase, on every sample including the silences.
inline int32_t TruncatedSine(uint32_t phase) {
  return kBraidsSine[(phase >> 22) & 0xff];
}

// The float macros arrive as 0..1; Braids' knobs are 15-bit. Matches
// render_braids_model.cc's own ToParameter, so an A/B case's `p1` and the
// engine's `timbre` land on the SAME integer.
inline int32_t ToBraidsParameter(float normalized) {
  float value = normalized * 32767.0f;
  CONSTRAIN(value, 0.0f, 32767.0f);
  return static_cast<int32_t>(value);
}

}  // namespace

void QuestionMarkEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

// digital_oscillator.cc:2241-2248. Braids reaches this through strike_, which
// MacroOscillator::Strike sets (macro_oscillator.h:80-82) and which a
// shape change also sets (:60-65). Here it is a trigger, and it restarts the
// transmission from the lead-in.
void QuestionMarkEngine::Strike() {
  key_ = 0;
  tick_phase_ = 0;
  ticks_left_ = kQuestionMarkLeadInTicks;
  symbol_index_ = static_cast<uint32_t>(-1);
  seed_ = kQuestionMarkSeedAtStrike;
}

void QuestionMarkEngine::Reset() {
  phase_ = 0;
  Strike();
  rng_state_ = kQuestionMarkRngSeed;
  rng_state_aux_ = kQuestionMarkRngSeedAux;
  dc_state_ = 0.0f;
  dc_state_aux_ = 0.0f;
}

void QuestionMarkEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    Strike();
  }

  const bool stereo = PLAITS_STEREO_QUESTION_MARK && parameters.stereo;

  // SPEC R4: use BRAIDS' own ceiling, which is tighter than the note 136
  // plaits::NoteToFrequency CONSTRAINs at. ComputePhaseIncrement saturates at
  // MIDI 127.9921875 (digital_oscillator.cc:51-53), so the module's carrier
  // tops out at 13.28 kHz; without this clamp the port plays 19965 Hz at note
  // 135 where Braids plays 13281, seven semitones sharp. NoteToFrequency
  // returns cycles per OUTPUT sample and the internal rate is 2x that, so the
  // substep increment is half the output-rate figure.
  float note = parameters.note;
  if (note > kQuestionMarkHighestNote) {
    note = kQuestionMarkHighestNote;
  }
  const float increment_per_substep = NoteToFrequency(note) * 0.5f;
  const uint32_t increment = static_cast<uint32_t>(
      increment_per_substep * 4294967296.0f);

  // Braids' two knobs, in Braids' own integer domain.
  const int32_t timbre = ToBraidsParameter(parameters.timbre);
  const int32_t color = ToBraidsParameter(parameters.harmonics);

  // digital_oscillator.cc:2252-2253, verbatim -- both are 96 kHz constants and
  // the loop below runs at 96 kHz, so neither needs re-deriving (SPEC R5).
  const uint32_t dit_duration = static_cast<uint32_t>(
      kQuestionMarkDitBase + ((32767 - timbre) >> 2));
  const int32_t noise_threshold =
      kQuestionMarkNoiseFloorBase + (color >> 3);

  // The two spare macros, both splitting what COLOR welds together. Exactly
  // the module at 0.5: ApplyMacro returns its stock argument at the midpoint,
  // so bed_gain is 32768 (unity in Q15) and grit is `color` itself.
  const int32_t bed_gain = static_cast<int32_t>(
      ApplyMacro(1.0f, 0.0f, kQuestionMarkMaxBed, parameters.morph) * 32768.0f);
  const int32_t grit = static_cast<int32_t>(
      static_cast<float>(color) *
      ApplyMacro(1.0f, 0.0f, kQuestionMarkMaxGrit, parameters.macro));

  uint32_t phase = phase_;
  uint32_t rng_state = rng_state_;
  uint32_t rng_state_aux = rng_state_aux_;

  while (size--) {
    int32_t accumulator = 0;
    int32_t accumulator_aux = 0;

    // Two substeps at Braids' own 96 kHz. Everything inside this loop is
    // digital_oscillator.cc:2254-2298 in its original order -- the tone is
    // read BEFORE the tick boundary and the noise AFTER it, which matters:
    // the boundary resets the phase, so a symbol's first noise sample reads
    // the ring modulator at a quarter cycle while its tone still saw the old
    // phase and the old gate.
    for (int j = 0; j < 2; ++j) {
      phase += increment;

      // :2257-2261
      const int32_t tone = key_
          ? (InterpolateSine(phase) * 3) >> 2
          : 0;

      // :2262-2279
      if (++tick_phase_ > dit_duration) {
        --ticks_left_;
        if (ticks_left_ == 0) {
          ++symbol_index_;
          key_ = !key_;

          const size_t address = symbol_index_ >> 2;
          const size_t shift = (symbol_index_ & 0x3) << 1;
          ticks_left_ = static_cast<int16_t>(
              (2 << ((kMorseCode[address] >> shift) & 3)) - 1);
          // :2271-2275 -- 15 is the terminator, not a duration.
          if (ticks_left_ == 15) {
            ticks_left_ = kQuestionMarkTerminatorTicks;
            key_ = 0;
            symbol_index_ = static_cast<uint32_t>(-1);
          }
          phase = 1L << 30;
        }
        tick_phase_ = 0;
      }

      // :2280-2290 -- an unbounded random walk, its magnitude clamped between
      // the COLOR-set floor and 16000. Shared by both channels in stereo, so
      // the bed's slow swell stays coherent across the image.
      seed_ += NextNoiseSample(&rng_state) >> 2;
      int32_t intensity = seed_ >> 8;
      if (intensity < 0) {
        intensity = -intensity;
      }
      if (intensity < noise_threshold) {
        intensity = noise_threshold;
      }
      if (intensity > kQuestionMarkMaxIntensity) {
        intensity = kQuestionMarkMaxIntensity;
      }

      // :2291-2292, then the Bed macro as a plain gain on the result so that
      // noon leaves the integer path untouched.
      int32_t noise = (NextNoiseSample(&rng_state) * intensity) >> 15;
      noise = noise * TruncatedSine(phase) >> 15;
      if (bed_gain != 32768) {
        noise = noise * bed_gain >> 15;
      }

      // :2293-2297
      int32_t sample = tone + noise;
      CLIP(sample);
      const int32_t distorted = sample * sample >> 14;
      sample += static_cast<int32_t>(
          (static_cast<int64_t>(distorted) * grit) >> 15);
      CLIP(sample);
      accumulator += sample;

      if (stereo) {
        // The right channel shares the keying, the phase and the intensity
        // walk, and draws only its noise SAMPLE from its own LCG.
        int32_t noise_aux =
            (NextNoiseSample(&rng_state_aux) * intensity) >> 15;
        noise_aux = noise_aux * TruncatedSine(phase) >> 15;
        if (bed_gain != 32768) {
          noise_aux = noise_aux * bed_gain >> 15;
        }
        int32_t sample_aux = tone + noise_aux;
        CLIP(sample_aux);
        const int32_t distorted_aux = sample_aux * sample_aux >> 14;
        sample_aux += static_cast<int32_t>(
            (static_cast<int64_t>(distorted_aux) * grit) >> 15);
        CLIP(sample_aux);
        accumulator_aux += sample_aux;
      } else {
        // Mono AUX: the keyed carrier alone, before the bed and the grit.
        accumulator_aux += tone;
      }
    }

    // 2-tap box decimator (see the header's RATE note), folded together with
    // the int16 -> float scaling.
    const float raw = static_cast<float>(accumulator) * (0.5f / 32768.0f);
    const float raw_aux =
        static_cast<float>(accumulator_aux) * (0.5f / 32768.0f);

    // The squared-distortion term is strictly positive, so this signal carries
    // real DC (up to 0.19 in Braids itself at COLOR 1, more once Grit opens).
    // R1: an engine with a DC blocker registers negative gains and takes the
    // limiter path.
    ONE_POLE(dc_state_, raw, kQuestionMarkDcCoefficient);
    ONE_POLE(dc_state_aux_, raw_aux, kQuestionMarkDcCoefficient);
    *out++ = raw - dc_state_;
    *aux++ = raw_aux - dc_state_aux_;
  }

  phase_ = phase;
  rng_state_ = rng_state;
  rng_state_aux_ = rng_state_aux;
}

}  // namespace plaits
