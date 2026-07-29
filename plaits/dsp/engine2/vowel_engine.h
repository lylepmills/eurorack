// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' VOWL model: three formant oscillators read out of 16-step tables,
// gated by a ramp that restarts on every pitch period, summed and driven
// through a tanh shaper.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderVowel
// (braids/digital_oscillator.cc:469).
//
// WHICH BRAIDS KNOB DOES WHAT, with line numbers, because the whole port
// depends on reading these two right:
//
//   digital_oscillator.cc:473  vowel_index   = parameter_[0] >> 12
//   digital_oscillator.cc:474  balance       = parameter_[0] & 0x0fff
//   digital_oscillator.cc:475  formant_shift = 200 + (parameter_[1] >> 6)
//
// `parameter_[0]` is TIMBRE and `parameter_[1]` is COLOR (macro_oscillator.cc:
// 356, set_parameters(parameter_[0], parameter_[1])). So TIMBRE walks a
// 9-frame phoneme table -- its top three bits pick the frame pair, its low
// twelve crossfade between them -- and COLOR is a single multiplier, 200..711,
// on all three formant frequencies at once, i.e. a 3.555x vocal-tract scale.
// Neither knob touches amplitude directly; the amplitudes ride along with the
// phoneme crossfade.
//
// The port SWAPS them onto TIMBRE and MORPH so that the three vocal engines
// agree: `speech` passes MORPH as the phoneme/vowel and TIMBRE as the formant
// shift (speech_engine.cc:103-110 into sam_speech_synth.cc:112), and vowel-fof
// already swapped for the same reason. Two vocal engines with inverted axes is
// worse than a naming deviation.
//
//   TIMBRE     Shift      Braids' COLOR: the 200..711 formant multiplier.
//   MORPH      Vowel      Braids' TIMBRE: the walk across the nine frames.
//   HARMONICS  Spread     scales formants 2 and 3 against formant 1, 0.5x to
//                         2x. Braids scales all three together and has no way
//                         to change the spacing.
//   MACRO      Grain      length of the per-period ramp, 0.2x to 3x. Braids
//                         welds it to exactly one period (`255 - phase >> 24`).
//
// LINEAGE, and the honest version of it. Plaits' `speech` engine contains a
// direct descendant of this model: SAMSpeechSynth is the same instrument in
// float. Its phoneme table is the same data. kSAMNumVowels is 9 and
// kSAMNumConsonants is 8 (sam_speech_synth.h:37-38); the amplitude indices are
// identical row for row across all 17 frames, checked entry by entry against
// vowels_data and consonant_data; and the frequencies are Braids' bytes scaled
// by a median of 2.241 (2.10 to 2.25 over the 51 entries, the low end being
// rounding on the small bytes) -- Braids vowel 0 is {27,40,89}, SAM row 0 is
// {60,90,200}. Its
// amplitude ladder `formant_amplitude_lut` (sam_speech_synth.cc:81) is
// 0.03125 * exp(0.184 * a), which is Braids' `gains = exp(0.184 * arange(16))`
// (braids/resources/waveforms.py:46) over 32. Its formant shift is
// `1 + timbre * 2.5`, a 3.5x range against Braids' 3.555x. Its envelope is
// `s *= (1 - phase_)`, which is Braids' ramp. Pure SAM is reachable in
// `speech` at HARMONICS = 1/6, where the model blend lands on it exactly.
//
// So this is not a model `speech` cannot make. What it is is the pre-float
// version, and four things survive the difference:
//
//   1. the formant oscillators are read at 16 steps per cycle
//      (`phaselet = (phase >> 24) & 0xf0`, :510), against the 512 steps SAM
//      gets out of lut_sine (`SineRaw`, sine_oscillator.h:74-76, with
//      kSineLUTBits = 9). Neither side interpolates; the difference is 32x in
//      resolution, and the staircase is most of the timbre.
//   2. the amplitude is a 4-bit index BAKED INTO the table -- the entry at
//      amplitude 1 takes the values {0,+-2,+-3,+-4,+-5}, so the quiet formants
//      are quantised to a few levels, where SAM applies a float gain.
//   3. the THIRD formant is a square wave (`wav_formant_square`, :519), not a
//      sine. SAM runs three sines.
//   4. the sum goes through `ws_moderate_overdrive` (:529), which is
//      scale(tanh(2x)) -- 2.07x of gain on small signals, read off the table's
//      own slope, and hard compression on loud ones. SAM has no shaper.
//
// The engine is worth having for that texture, and the two new macros are
// chosen to be axes `speech` has no control for, but the resemblance is real
// and is stated here rather than talked around.
//
// RATE. RenderVowel is a plain `while (size--)` loop (:505) with no `size -= 2`
// and no internal oversampling, so it is a 96 kHz algorithm and every rate
// constant in it is expressed against 96 kHz. Rather than re-derive them, the
// port runs its inner loop 2x oversampled -- at exactly Braids' 96 kHz -- and
// decimates. Three things then transfer verbatim instead of moving:
//
//   * the formant phase increments, `f * 0x1000 * formant_shift` (:481, :493);
//   * the consonant length, 160 blocks of 24 (:478) = 3840 samples;
//   * the per-sample draw on Braids' LCG (:522), which at the matched rate and
//     the matched 24-sample block leaves the port picking the same consonant
//     frames the module picks. That one is a nicety, not a necessity: seeded
//     0x22 instead of 0x21 the port chooses a different consonant on every
//     strike, and the 4 Hz triggered A/B case moves from 0.21 dB to 0.27 dB.
//
// Matching the rate is NOT a nicety. The 16-step staircase and the square third
// formant are not band-limited: at COLOR = 1 the highest third-formant
// increment Braids can reach is 7876 Hz (consonant frame 0, whose byte is 121 --
// though that frame carries amplitude index 0 on formant 3, so the loudest
// AUDIBLE third formant is 7226 Hz on consonant frame 4 and 7160 Hz across the
// vowels), and the 11th harmonic of a 7160 Hz square is 78.8 kHz, which folds
// to 17.2 kHz at 96 kHz sampling; the 13th folds to 2.9 kHz. That
// folded content is part of what the module sounds like, and the fold lands
// somewhere else at 48 kHz. Measured, by building the 48 kHz variant -- one
// sample per output, increments doubled, consonant halved to 1920, no
// decimator -- and running the same seven A/B cases: spectral difference goes
// from 0.05-0.21 dB to 0.38-0.89 dB, and the error is not spread out, it sits
// in 5-24 kHz (+1.7 to +3.3 dB in the 10-20 kHz band, up to +8.2 dB above
// 20 kHz) exactly where the misplaced fold products are.
//
// The decimator is a 31-tap Blackman-Harris halfband, 17 non-zero taps folded
// to 9 multiplies, unity DC gain: -0.36 dB at 18 kHz, -6.02 dB at 24 kHz,
// -17.9 dB at 28 kHz, -41.7 dB at 32 kHz (computed from the coefficients that
// are in the .cc). The reference renderer decimates with a 127-tap sinc whose
// stopband is below -90 dB, so the port keeps alias residue in the top octave
// that the reference does not -- a declared residue per SPEC R7, and it is
// where the A/B's remaining difference lives: the 20.5-24 kHz band runs -0.3
// to -2.6 dB across the seven cases, while every band from 80 Hz to 20.5 kHz
// stays inside 0.5 dB.
//
// The two bands BELOW 80 Hz can run wider than that -- up to 18 dB on the vowel
// sweep -- and that is the DC blocker, whose corner is about 19 Hz, against a
// module that has none. Where those bands run wide they are empty: the vowel
// sweep's -18.0 dB at 20-40 Hz is 0.2% of the energy and its -5.7 dB at
// 40-80 Hz is 0.3%. Where they carry real energy they are close: shift-low puts
// 3.0% of its energy in 40-80 Hz and differs there by 0.31 dB.
//
// MEASURED against the module (tests/ab.json, seven cases at both ends of both
// Braids axes plus a 4 Hz triggered one): AC RMS within 0.05 dB, pitch within
// 0.2 cents, spectral difference 0.05-0.21 dB.
//
// OUT: the shaped stack, which is Braids.
//
// AUX (mono): the same stack AHEAD of the overdrive -- the clean formant sum,
// the same "signal ahead of the nonlinearity" idiom vowel-fof puts on aux. It
// is the QUIETER of the two before gain -- the shaper's small-signal slope is
// 2.07x, read straight off the table (entries 127/128/129 are -531/0/531 over
// an index step of 1/128) -- so aux_gain is 1.25 against out_gain 0.7. The raw
// difference measures 5.3 dB across the declared scenarios and that pair of
// gains lands the two channels within 0.3 dB RMS of each other.
//
// In stereo OUT/AUX become L/R and the two sides take opposing 2% formant
// shifts, so the voice widens without either side becoming a different vowel.
// Measured over a 1 s sweep of all four macros with a 4 Hz retrigger: peaks
// 0.933 and 0.918 before gain, L/R correlation 0.68 -- wide, and still one
// instrument rather than two.
//
// Declared deviations from Braids:
//   - the decimator above, in place of Braids' native 96 kHz output.
//   - the sum is carried in int and the shaper output in float; Braids keeps
//     both in int16. No wrap is reachable on either side: the largest formant
//     stack over every reachable amplitude triple is 108, and 108 * 255 =
//     27540 against int16's 32767 (measured over all 9 vowel frames, all 4096
//     crossfade positions and all 8 consonant frames).
//   - frame data is recomputed once per Plaits block where Braids recomputes
//     once per 24-sample block. At plaits::kBlockSize (12 output samples = 24
//     sub-samples at the matched internal rate) those are the same 24 samples;
//     a host rendering longer blocks would step the parameters more coarsely.
//   - a one-pole DC blocker on each output, which Braids does not have. Per
//     SPEC R1 that forces negative gains and the limiter path.

#ifndef PLAITS_DSP_ENGINE2_VOWEL_ENGINE_H_
#define PLAITS_DSP_ENGINE2_VOWEL_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

const int kVowelNumFormants = 3;

// The port's inner loop runs at Braids' own 96 kHz.
const int kVowelOversampling = 2;

// The 31-tap halfband decimator's history, rounded up to a power of two so the
// circular index masks. Its furthest reach is x[n - 30].
const int kVowelFirMask = 31;
const int kVowelFirSize = 32;

// digital_oscillator.cc:478, `consonant_frames = 160`, decremented once per
// 24-sample block: 3840 samples at 96 kHz, which is 40 ms.
const int kVowelConsonantSamples = 160 * 24;

// digital_oscillator.cc:475. COLOR spans 200..711, a 3.555x multiplier.
const int kVowelMinFormantShift = 200;

// Braids' real pitch ceiling for this shape is NOT `kHighestNote = 140 * 128`
// (digital_oscillator.cc:43). That clamp lives at :120-121, AFTER
// ComputePhaseIncrement has already run, so it never bounds the block it is in.
// What actually bounds the increment is ComputePhaseIncrement's own first line
// (:52-53): `if (midi_pitch >= kPitchTableStart) midi_pitch = kPitchTableStart
// - 1`, with kPitchTableStart = 128 * 128 (:44). So VOWL saturates at note
// 16383/128 = 127.9921875 and plays no higher however far the pitch CV goes.
// The port uses Braids' actual ceiling rather than the constant that looks like
// one (SPEC R4). (plaits::NoteToFrequency CONSTRAINs at note 136 on its own,
// which is why the old 140 was never reached either -- but 136 is not 128.)
const float kVowelHighestNote = 127.9921875f;

// SPEC R4: every increment is bounded so that, expressed at the 48 kHz output
// rate, it stays under plaits::kMaxFrequency (0.25). The inner loop is 2x, so
// the internal bound is half of that. The clamp is inactive across Braids'
// whole range -- the largest reachable formant increment is 121 * 0x1000 * 711
// = 0.08205 of full scale (7876 Hz), well under 0.125 -- and exists only to
// bound the Spread macro, which Braids has no equivalent of.
const uint32_t kVowelMaxFormantIncrement = 536870912u;  // 0.125 * 2^32

// Spread scales formants 2 and 3 against formant 1. Half to double is enough
// to pull an /a/ toward an /u/ and out past an /i/ without leaving the range
// where the stack still reads as a voice.
const float kVowelMinSpread = 0.5f;
const float kVowelMaxSpread = 2.0f;

// Grain is the length of the per-period ramp as a multiple of the period.
// Below 1 the burst ends early and the formant rings shorter and brighter;
// above 1 the ramp is still high when the period restarts, so the reset
// becomes an edge. 1.0 is Braids exactly.
const float kVowelMinGrain = 0.2f;
const float kVowelMaxGrain = 3.0f;

// Opposing formant shifts for the two stereo channels. Small: the formant
// pattern is the vowel's identity, and a wide split would make L and R
// different phonemes rather than one voice with width.
const float kVowelStereoDetune = 0.02f;

class VowelEngine : public Engine {
 public:
  VowelEngine() { }
  ~VowelEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_VOWEL; }

 private:
  int NextRandomSample();

  uint32_t phase_;
  uint32_t phase_increment_;
  uint32_t formant_phase_[kVowelNumFormants];
  uint32_t formant_phase_r_[kVowelNumFormants];

  // Braids' per-block frame state: state_.vow.formant_increment /
  // formant_amplitude / noise, held between blocks because the consonant
  // branch deliberately stops refreshing them.
  uint32_t formant_increment_[kVowelNumFormants];
  int formant_amplitude_[kVowelNumFormants];
  int noise_;
  int consonant_samples_;
  bool strike_pending_;

  // stmlib::Random seeded as the module seeds it (random.cc:34).
  uint32_t rng_state_;

  float fir_a_[kVowelFirSize];
  float fir_b_[kVowelFirSize];
  int fir_write_;

  float dc_input_;
  float dc_output_;
  float dc_input_aux_;
  float dc_output_aux_;

  DISALLOW_COPY_AND_ASSIGN(VowelEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_VOWEL_ENGINE_H_
