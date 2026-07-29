// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' WAVE PARAPHONIC model: four wavetable voices reading one scan line,
// three of them detuned to a chord.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderWaveParaphonic
// (braids/digital_oscillator.cc:1755-1831).
//
// LINEAGE. Plaits' own ChordEngine covers most of this ground: the upper half
// of its MORPH already runs its five wavetable voices, tuned to a chord, over a
// curated wave line (chord_engine.h:69, chord_engine.cc:141), and its TIMBRE
// (ComputeChordInversion, chord_engine.cc:132-135) already fans voices by
// octaves. So the two axes this
// port adds are chosen to be ones ChordEngine has no control for -- a per-voice
// WAVE fan and a continuous INTERVAL scaler -- and the octave fan an earlier
// design proposed for MACRO is deliberately absent, because ChordEngine's
// inversion is that control already.
//
// What the Braids model still has that ChordEngine does not: its own 17-row
// chord list, whose first two rows are sub-semitone unisons ({2, 4, 6} and
// {16, 32, 48} in 1/128 semitone, i.e. inside 4.7 and 37.5 cents); a 33-step
// scan line over a different selection of waves; and no per-voice registration
// or amplitude shaping at all -- four equal voices, nothing else.
//
// THE BRAIDS CONTROLS, with line numbers. RenderWaveParaphonic reads
// parameter_[0] and parameter_[1] DIRECTLY -- it uses none of the
// BEGIN_INTERPOLATE_PARAMETER_n macros, so fold_engine.h's suffix trap cannot
// arise here. There is no ambiguity to resolve: the indices are written out.
//
//   parameter_[0] = TIMBRE -> the wave. :1794-1796 selects
//     mini_wave_line[parameter_[0] >> 10] and its successor, and crossfades
//     them by wave_xfade = (uint16_t)(parameter_[0] << 6). 32 steps of 1024
//     with a 16-bit fraction inside each; the two expressions are continuous
//     across a step boundary.
//   parameter_[1] = COLOR -> the chord. :1777-1792. chord_integral =
//     parameter_[1] >> 11 picks a row of chords[17][3]; chord_fractional =
//     (uint16_t)(parameter_[1] << 5) is then flattened to 0 below 30720 and to
//     65535 at/above 34816 and otherwise expanded by 16, which gives each row
//     a dead zone and a fast ramp between rows rather than a linear morph.
//     Each of the three upper voices takes
//     detune = d1 + ((d2 - d1) * chord_fractional >> 16), an INTEGER in
//     1/128 semitone, and plays at ComputePhaseIncrement(pitch_ + detune).
//     Voice 0 always plays the root, undetuned.
//
// The port keeps that arithmetic in integers so the truncation survives: the
// detunes are quantised to 1/128 semitone before they reach the pitch, exactly
// as the module quantises them, rather than being computed as exact floats.
//
//   HARMONICS  Chord      Braids' COLOR: the 17-row chord list.
//   TIMBRE     Wave       Braids' TIMBRE: the 33-step scan line.
//   MORPH      Fan        offsets each voice further along the scan line, so
//                         the four read different waves. Zero at noon, and
//                         signed: below noon the root keeps the brighter wave,
//                         above it the top of the chord does. Braids has no
//                         such control, and neither does ChordEngine.
//   MACRO      Spread     scales every chord interval by 0 to 2x around
//                         Braids' own voicing at 1.0. At 0 the chord collapses
//                         to a unison; at 2 every interval doubles, so a fifth
//                         opens to a ninth and a tritone to an octave. This is
//                         an interval scaler, not the octave fan
//                         ComputeChordInversion already performs.
//
// STRIKE, and why no core file is edited. :1759-1764 randomises all four
// phases from Random::GetWord() on a strike, which is what keeps the
// near-unison rows from opening as one comb-filtered impulse. SPEC 7.2's
// design reached that by adding a set_phase() to the SHARED
// plaits/dsp/oscillator/wavetable_oscillator.h, whose phase_ is private -- an
// edit no package digest covers. This engine needs no such edit because it
// does not drive a WavetableOscillator at all: it owns four float phases and
// a small readout of its own, so the randomisation is ordinary engine code.
//
// It draws from the same stmlib::Random, and arms the strike as a FLAG
// consumed inside Render() exactly as strike_ is, so exactly four words are
// drawn before the first sample no matter how many times Init()/Reset() run
// beforehand. Measured, not assumed: a probe printing Random::state() around
// the first Render finds the module's reference renderer and this engine both
// leaving it at 3819973861, four LCG steps from the 0x21 seed, so from a cold
// start the port's four opening phases are the module's own.
//
// RATE (R5). RenderWaveParaphonic ends in `size -= 2` (:1823), but that is a
// 2x UNROLL, not oversampling: each of the two writes gets its own phase
// advance (:1801-1804, :1812-1815) and its own set of table reads, so one
// output sample costs one phase step and the inner algorithm runs at Braids'
// full 96 kHz. Compare RenderWavetables (:1549-1560), which halves the
// increment and takes two sub-steps per output -- that one really is 2x
// oversampled. The distinction matters because it decides whether any constant
// has to move.
//
// Here nothing does. The function contains no filter, no envelope and no time
// constant of any kind: the chord detunes live in the pitch domain (1/128
// semitone), the wave scan is a table index, and the only rate-dependent
// quantity in the whole model is the phase increment itself, which Plaits
// derives from NoteToFrequency against kCorrectedSampleRate (R6). So the model
// transfers to 48 kHz verbatim, and the single consequence of the halved rate
// is aliasing.
//
// ANTI-ALIASING, stated honestly. There is none, on either side, and the port
// is the worse of the two.
//
// Braids point-samples a 129-byte wave with linear interpolation
// (Interpolate824, stmlib/utils/dsp.h:106-111) at 96 kHz and band-limits
// nothing. The port does the same at 48 kHz. A 128-point table carries to the
// 64th harmonic and there is no mipmap, so the readout is alias-free below
// f0 = 375 Hz -- MIDI 66.2 -- where Braids is alias-free below 750 Hz, MIDI
// 78.2. Above MIDI 66 this port aliases more than the module, and the A/B's
// high-note case (MIDI 69) measures it: +2.1 dB at 5-10 kHz, +4.2 dB at
// 10-20 kHz and +9.1 dB above 20 kHz, 1.35 dB overall.
//
// SPEC 7.2 was right to call the opposite claim false. Plaits'
// WavetableOscillator low-passes its readout at
// cutoff = min(wavetable_size * f0, 1.0f) (wavetable_oscillator.h:144,
// 163-166), and that expression saturates at 1.0 for every f0 above 1/128 --
// the same 375 Hz -- above which both one-poles are literally pass-through,
// across exactly the range where a wavetable oscillator needs help. Below it
// they are not harmless either: at MIDI 45 the coefficient is 0.294 and the
// pair is 4.5 dB down by the 20th harmonic and 17.4 dB down by the 64th, the
// top of the table. Braids has no such filter, so this port has none, and it
// makes no anti-aliasing claim at all.
//
// One thing that IS worth knowing about the readout: Plaits reads the K = 1
// INTEGRATED table by interpolating the integral and differencing the result,
// which -- since a linearly interpolated integral has constant slope inside a
// table cell -- reconstructs the wave as a zero-order HOLD, with images
// falling off as sinc(f/f_table) where Braids' linear interpolation falls off
// as sinc^2. The first version of this port did that and measured +16.3 dB at
// 10-20 kHz and +22.1 dB above 20 kHz against the reference on a case whose
// waves are exact matches. Differencing the table FIRST and interpolating
// second costs one extra table read and reproduces Braids' readout instead.
//
// WAVE DATA -- the largest declared deviation. Braids' wt_waves is not in the
// Plaits firmware, and vendoring the 33 waves this model scans would cost
// ~4.3 KB. Instead the port substitutes from wav_integrated_waves, which is
// already linked, for 66 bytes of index and gain table.
//
// That works at all because Plaits' bank 3 is GENERATED FROM THE SAME DATA:
// plaits/resources/wavetables.py's make_braids_family reads
// plaits/resources/waves.bin, which is byte-identical to braids/resources.cc's
// wt_waves (verified programmatically, all 33,024 bytes). 16 of the 33 scan
// slots therefore have an exact counterpart in the 192-wave bank; the other 17
// take the nearest wave in the bank instead, shortlisted by an offline
// symmetric per-octave-band comparison and then chosen by measuring each
// shortlisted candidate through the A/B itself.
//
// Per-slot A/B against the module, 8 s at MIDI 45 on the stock chord, with
// TIMBRE parked on each slot in turn (33 measurements, all in
// tests/ab.json's format):
//
//   the 16 exact slots  spectrum 0.05 to 0.29 dB, median 0.10
//                       AC RMS  -0.22 to +0.09 dB
//   the 17 substitutes  spectrum 0.57 to 2.24 dB, median 1.12
//                       AC RMS  -0.24 to +0.09 dB
//   every slot          pitch within 0.5 cents
//
// The worst slot is 31 (Braids wave 135) at 2.24 dB. Slot 32 -- Braids 174,
// the far end of TIMBRE -- is exact, and so is the run of slots 21 to 25 just
// above the middle of the line.
//
// Two caveats on "exact". First, make_braids_family(fix=True) replaces each
// wave's PHASE spectrum with a uniform -90 degrees and keeps only its
// magnitudes, so 15 of the 16 exact slots carry the Braids wave's harmonic
// levels exactly while being a different waveform with a much higher crest
// factor; only slot 32 comes from the fix=False family and is the actual
// waveform. Second, that shows up when the four voices are crossfaded or
// summed at near-coincident intervals: a single exact wave A/Bs at 0.37 dB
// (tests/ab.json wave-pure) while a 40% crossfade between two exact waves
// reads 0.65 dB (stock-mid), because the two waves' relative harmonic phases
// are not the module's.
//
// wavetables.py also peak-normalises every wave, which Braids' 8-bit data is
// not, so a 33-entry gain table in 1/128 steps restores each slot's Braids
// RMS. The gains span 0.734 to 1.703 and quantise to within 0.042 dB.
//
// MEASUREMENT WINDOW, worth knowing before reading any number here. The stock
// chord row is {7, 12.023, 19.039} semitones, so the octave and twelfth voices
// sit 2.3 and 3.9 cents off exact ratios and their coincident harmonics beat
// at a fraction of a hertz. A short render catches an arbitrary point of that
// beat. Slot 17 measured over 2 / 4 / 8 / 16 s reads 1.49 / 0.29 / 0.23 /
// 0.17 dB and -1.49 / -0.58 / -0.21 / -0.11 dB AC RMS, and on the
// near-unison chord row, where the four voices share harmonic indices, it
// reads 0.05 dB and +0.03 dB at 2 s. The per-slot table above is at 8 s for
// that reason.
//
// OUT: the four voices summed and averaged, exactly as :1806-1810 and
// :1817-1822 do.
// AUX (mono): the root voice alone, at twice the level it contributes to OUT
// so it stands up as a layer. In stereo OUT/AUX become L/R and the four voices
// take fixed equal-power pan positions.
//
// Declared deviations from Braids:
//   - 17 of the 33 scan waves are substituted, and 15 of the other 16 are
//     phase-flattened by Plaits' own generator. Numbers above.
//   - a per-slot gain table restores Braids' relative wave levels, which
//     wavetables.py's peak normalisation removes.
//   - the wave is read from an int16 table recovered from Plaits' integrated
//     waves, so the port does not carry Braids' 8-bit wave quantisation.
//   - 48 kHz against Braids' 96 kHz, with the aliasing consequence above.
//   - the analytic peak is 1.703, above the CONSTRAIN bound, so the port can
//     clip where Braids (a sum of four int16s shifted right by two) cannot.
//     Reaching it needs all four voices peaking on one sample at the loudest
//     slot; the worst measured is 0.989 over a 30 s note built to force it.
//     See the note at the output stage in the .cc.
//   - the waves carry no DC: wavetables.py mean-removes each one, where
//     Braids plays its 8-bit table with whatever offset it has.
//   - Spread and Fan have no counterpart in the module; both are neutral at
//     MACRO/MORPH noon, which is where every A/B case holds them.

#ifndef PLAITS_DSP_ENGINE2_WAVE_PARAPHONIC_ENGINE_H_
#define PLAITS_DSP_ENGINE2_WAVE_PARAPHONIC_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids plays the root plus three chord voices (digital_oscillator.cc:1766).
const int kWaveParaphonicNumVoices = 4;

// 33 entries, 32 steps of 1024 counts of parameter_[0] between them
// (digital_oscillator.cc:1794-1796).
const int kWaveParaphonicNumSlots = 33;
const int kWaveParaphonicStep = 1024;

// The scan line is 32 steps wide, so a fan of 0.15 per voice puts the top
// voice up to 0.45 -- about fourteen steps -- away from the root's wave.
// Wide enough to hear as four different waves, narrow enough that the four
// stay recognisably one instrument.
const float kWaveParaphonicMaxFan = 0.15f;

// Braids' own voicing is Spread 1.0. Zero collapses the chord to a unison,
// 2.0 doubles every interval.
const float kWaveParaphonicMinSpread = 0.0f;
const float kWaveParaphonicMaxSpread = 2.0f;

// Equal-power pan positions, root centred. Only read in a stereo build.
const float kWaveParaphonicPan[kWaveParaphonicNumVoices] = {
  0.5f, 0.16f, 0.84f, 0.34f
};

class WaveParaphonicEngine : public Engine {
 public:
  WaveParaphonicEngine() { }
  ~WaveParaphonicEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_WAVE_PARAPHONIC; }

 private:
  float phase_[kWaveParaphonicNumVoices];
  float frequency_[kWaveParaphonicNumVoices];

  // Braids' strike_ (digital_oscillator.h:353): a flag set by a trigger and
  // consumed inside Render, so the four Random::GetWord() draws happen once,
  // just before the first sample they affect.
  bool strike_;

  DISALLOW_COPY_AND_ASSIGN(WaveParaphonicEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_WAVE_PARAPHONIC_ENGINE_H_
