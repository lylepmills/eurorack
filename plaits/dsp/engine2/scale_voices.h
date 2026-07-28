// Copyright 2012 Emilie Gillet.
// Copyright 2018 Tom Burns.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Shared scale-degree machinery for the two Braids Renaissance ports,
// `diatonic-chord` and `scale-stack`.
//
// Upstream is Tom Burns' Braids Renaissance (boourns/eurorack-renaissance),
// whose CHORD_* and STACK_* models both funnel into one
// DigitalOscillator::renderChord: voice 0 plays the root, and every other voice
// is placed at a SCALE DEGREE above it, resolved through Braids' built-in
// quantizer codebook. That is the whole idea, and it is what separates these
// from Plaits' own `chords` engine -- the chord's quality is not stored
// anywhere, it falls out of where the played note lands in the scale, so one
// knob position gives Cmaj7 on the tonic and Am7 on the sixth.
//
// Plaits has no quantizer and no scale setting, so the scale ships with the
// engine and MACRO selects it. Eight are enough to cover the gesture, and the
// last two are the interesting ones: in a pentatonic or whole-tone scale a
// "third" is not a third, and the chord shapes bend accordingly.
//
// THE ROOT IS C. Braids resolved the scale against the quantizer's own root;
// here degree 0 is the nearest scale tone at or below the played note measured
// from C, which is the same convention Braids shipped with by default.
//
// FINE PITCH SURVIVES QUANTISATION. Braids kept `fm = pitch_ -
// codebook_[index]` and added it back to every voice, so bends and FM moved the
// whole chord rather than being swallowed by the quantizer. The residual here
// does the same job.

#ifndef PLAITS_DSP_ENGINE2_SCALE_VOICES_H_
#define PLAITS_DSP_ENGINE2_SCALE_VOICES_H_

#include "stmlib/stmlib.h"

#include "plaits/dsp/dsp.h"

namespace plaits {

const int kScaleVoicesNumScales = 8;
const int kScaleVoicesMaxDegrees = 7;
const int kScaleVoicesMaxVoices = 6;

// Widest reach either engine asks for: `scale-stack` at its top span places its
// last voice 4 * 16 = 64 degrees up, which in a pentatonic scale is nearly 13
// octaves. Voices past the audible range are dropped rather than aliased.
const int kScaleVoicesMaxDegreeOffset = 64;

// TIMBRE's ensemble detune, in cents, applied symmetrically (+ to one voice, -
// to the next) so the chord itself stays in tune while the voices beat.
const float kScaleVoicesMaxDetuneCents = 25.0f;

// Renaissance drove its sine model through Braids' `ws_sine_fold` waveshaper
// with a gain running 2048..32768. This is the same move: TIMBRE folds, and
// only in the sine region, which is exactly where upstream put it.
const float kScaleVoicesMaxFoldDrive = 4.0f;

// Voices above this normalised frequency are muted rather than rendered. A
// naive saw at 0.45 of the sample rate is nothing but alias.
const float kScaleVoicesMaxVoiceFrequency = 0.45f;

struct Scale {
  const int8_t* semitones;
  int num_degrees;
};

// The eight MACRO positions, in order.
extern const Scale kScaleVoicesScales[kScaleVoicesNumScales];

// Resolves a played note onto the scale: returns the index of the nearest
// scale degree (counting across octaves, so degree n + num_degrees is one
// octave up) and writes the leftover semitones to `residual`.
int QuantizeToScale(float note, int scale, float* residual);

// The MIDI note of an absolute scale degree.
float ScaleDegreeToNote(int degree, int scale);

// A bank of naive oscillators sharing one waveform axis. Naive rather than
// band-limited on purpose: Braids rendered these models with plain phase
// accumulators, that raw stacked-oscillator sound is the engine's identity, and
// six band-limited voices would not fit the CPU budget in any case. What the
// original could lean on and this cannot is its 96 kHz sample rate, so the two
// waveforms with a real discontinuity get a PolyBLEP -- a few operations per
// wrap, far cheaper than a full BLEP oscillator, and enough to keep the top of
// the waveform axis from turning to gravel an octave down from where Braids
// did.
class ScaleVoiceBank {
 public:
  ScaleVoiceBank() { }
  ~ScaleVoiceBank() { }

  void Init();
  void Reset();

  // `notes` holds `num_voices` MIDI notes; voice 0 is the root and is the one
  // written to `aux`. `waveform` runs sine -> triangle -> saw -> square,
  // `detune_cents` and `fold` come off TIMBRE.
  void Render(
      const float* notes,
      int num_voices,
      float waveform,
      float detune_cents,
      float fold,
      float* out,
      float* aux,
      size_t size);

 private:
  float phase_[kScaleVoicesMaxVoices];
  float dc_in_;
  float dc_out_;
  float dc_aux_in_;
  float dc_aux_out_;

  DISALLOW_COPY_AND_ASSIGN(ScaleVoiceBank);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_SCALE_VOICES_H_
