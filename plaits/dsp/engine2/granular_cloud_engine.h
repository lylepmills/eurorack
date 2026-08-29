// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' GRANULAR_CLOUD model: four sine grains under Hann windows, each one
// respawned by a coin flip when its window runs out, each respawn taking a
// fresh random pitch offset.
//
// The algorithm is Emilie Gillet's DigitalOscillator::RenderGranularCloud
// (braids/digital_oscillator.cc:2015-2073).
//
// LINEAGE. Plaits has two engines in this neighbourhood and this is neither of
// them. GRANULAR FORMANT (plaits/dsp/engine/grain_engine.cc) is two grainlet
// oscillators plus a Z-oscillator: its "grains" are formant bursts locked to a
// carrier, one per cycle, so it is a synchronous formant synth and stays
// pitched at every setting. PARTICLE NOISE (plaits/dsp/engine/particle_engine.cc)
// is six random impulses through resonant band-passes -- stochastic, but the
// grain is an impulse response, not a windowed tone, and the randomness lands
// on the filter frequency. GRANULAR_CLOUD is the asynchronous one: grain
// onsets are a Bernoulli process, grains overlap freely because nothing
// synchronises them, and the random variable is the grain's own PITCH. Sweep
// its Scatter control and the same four voices leave pitch behind entirely:
// measured at note 72 with the longest grains, autocorrelation finds 87.2 Hz
// (a sixth-subharmonic lock on the 523 Hz carrier) at COLOR 0, 562 Hz at
// COLOR 0.5 -- pulled sharp, because the scatter reaches 1.496x up against
// 0.75x down -- and NOTHING at COLOR 1, the peak falling under the detector's
// periodicity gate. The port reproduces that: 87.4, 563.0, none. The Plaits
// pair have no control that does this.
//
// THE BRAIDS CONTROLS, with lines. Both come off the two knobs directly; there
// is no INTERPOLATE_PARAMETER macro anywhere in this function, so the
// suffix trap that cost fold a rewrite does not arise here -- but the mapping
// was still read off the source rather than assumed:
//
//   parameter_[0] is TIMBRE. Line 2028: `lut_granular_envelope_rate[
//     parameter_[0] >> 7] << 3` is the grain's envelope rate, so TIMBRE is
//     grain LENGTH. The table is 2^(i/64) * 2048 over i = 0..256, and the
//     window ends when envelope_phase passes 1<<24 (line 2023), so a grain
//     runs 1024 / 2^(i/64) samples at 96 kHz: 10.7 ms at TIMBRE 0 down to
//     0.67 ms at TIMBRE 1. Short grains die sooner, and on a fixed respawn
//     coin they therefore also respawn more often -- TIMBRE moves grain length
//     and grain rate together, which is why it reads as a single knob.
//
//   parameter_[1] is COLOR. Line 2031: `pitch_mod = Random::GetSample() *
//     parameter_[1] >> 16`, then lines 2032-2037 add `(phase_increment_ >> 8)
//     * (pitch_mod >> 8)` for a negative draw and `>> 7` for a positive one.
//     So COLOR is the width of a random PITCH offset applied once per grain,
//     as a linear multiple of the phase increment: at COLOR 1 the multiplier
//     spans 0.75x to 1.496x, deliberately asymmetric (the >>8 / >>7 split
//     halves the downward reach). At COLOR 0 every grain runs at the played
//     pitch.
//
//   HARMONICS  Scatter    Braids' COLOR: the per-grain random pitch offset.
//   TIMBRE     Grain      Braids' TIMBRE: grain length, long to short.
//   MORPH      Shape      where the Hann window peaks. Braids' window is fixed
//                         and symmetric; this slides the peak from late (a
//                         reversed swell) through Braids' Hann at noon to
//                         early (a struck grain).
//   MACRO      Density    the respawn probability. Braids fixes it at 0x4000
//                         in 0x10000 (line 2026), i.e. one chance in four per
//                         250 us slot; this runs it from 1/64 -- a sputtering,
//                         gapped cloud -- up to certainty, where the four
//                         voices never leave a gap.
//
// RATE (R5). RenderGranularCloud does NOT end in `size -= 2`: line 2044 is a
// plain `while (size--)` writing one sample per iteration, and there is no
// internal oversampling anywhere in the function. It is therefore a 96 kHz
// algorithm and EVERY rate constant had to be re-derived for 48 kHz:
//
//   - the envelope increment doubles. Braids' `<< 3` (line 2028) becomes
//     `<< 4` here, against the same 1<<24 endpoint, so grain length in
//     SECONDS is unchanged.
//   - the grain scheduler's period halves. Braids runs the respawn loop
//     (lines 2020-2040) once per Render call, and Braids' block is 24 samples
//     at 96 kHz (braids.cc kBlockSize; the reference renderer fixes it at 24
//     for this reason), so a grain gets a respawn chance every 250 us. This
//     port schedules on its own 12-sample counter at 48 kHz, which is the same
//     250 us. Plaits' kBlockSize happens to be 12 (plaits/dsp/dsp.h:51), so on
//     a stock block the counter fires on the block boundary exactly as Braids
//     does -- but the counter, not the block, is what defines the rate, so a
//     host calling Render() with any other size still gets Braids' density.
//     Verified: driven at 1, 7, 12 and 24 samples per call, the engine
//     measures -0.15 dB AC RMS and 0.30 dB spectrum against the reference in
//     all four cases, to the printed precision.
//
// DETERMINISM, and why the A/B is a STATISTICAL comparison. The model draws
// from Random::GetWord() / Random::GetSample() (lines 2026 and 2031), so
// nothing about it is reproducible sample-by-sample unless both sides run the
// same generator from the same seed through the same call sequence. This port
// does carry Braids' generator -- the same LCG, seeded from stmlib's own
// initial 0x21 (stmlib/utils/random.cc:34) at Init(), consuming exactly one
// word per respawn test and one more per successful respawn -- and Braids
// spends no randomness anywhere else in this shape's path, so the grain
// ONSETS and the grain PITCHES line up with the reference. That was measured,
// not assumed: against the reference the port correlates +0.998 over the first
// 50 ms, and its 1 ms RMS contour correlates +0.33 to +0.78 in EVERY second of
// a five-second render, where the same engine reseeded to three arbitrary
// other values correlates +0.01 to +0.04. So the schedule stays locked for the
// whole render.
//
// The carrier phase does not stay locked. Braids' phase increment comes from
// its own 96 kHz pitch table and this port's from NoteToFrequency(), derived
// from kCorrectedSampleRate -- a fixed +4.61 cents (SPEC R6) -- and grain
// phase is never reset (line 2030 sets the increment and leaves `phase` where
// it was). Over five seconds at note 45 that is about 1.5 cycles of drift, and
// it shows: the waveform correlation measured in 50 ms slices at t = 0, 1, 2,
// 3 and 4 s runs +0.998, -0.50, -0.82, +0.88, +0.15 -- large in magnitude,
// rotating in sign. Same grains, turning relative to each other.
//
// So tests/ab.json compares band energies over long windows. HOW MUCH OF THAT
// leans on the seed match was measured per case, by rebuilding with
// kGranularCloudSeed set to 0x12345, 0xdeadbeef and 0x7 and re-running the
// whole suite. It splits:
//
//   - the SHORT- and MID-grain cases converge without the seed. stock-mid,
//     unison-short, scatter-full and high-note stay inside the declared
//     tolerances at all three foreign seeds (AC RMS -0.46 to +0.14 dB,
//     spectrum 0.09 to 0.40 dB), against -0.15 to +0.08 dB and 0.10 to
//     0.30 dB seeded correctly. Those four ARE a test of the model.
//   - the LONG-grain cases do NOT. unison-long, pitched-mid and pitched-high
//     use TIMBRE 0, where a grain is 10.7 ms and five seconds holds too few
//     independent grain events for the level statistic to settle: reseeded
//     they swing -2.20 to +2.94 dB of AC RMS and up to 0.79 dB of spectrum,
//     which would FAIL the declared +/-0.5 dB. Their level tolerances
//     therefore do depend on the port drawing Braids' own sequence -- which
//     it does, and which is itself part of the port being faithful, but it
//     is a stricter claim than "the statistics converge" and should not be
//     read as the weaker one.
//
// The R5 conclusion does not depend on the seed either way: the corrected
// pitch on pitched-mid / pitched-high reads -0.3 / +0.1 cents seeded, and
// -0.3 to +0.8 cents across all three foreign seeds. A pitch tolerance is
// declared only where an f0 exists at all.
//
// TABLES. Both of Braids' tables are substituted by their generator formula
// rather than embedded, because neither is read in the inner loop
// (braids/resources/lookup_tables.py:120-131), and both substitutions were
// measured against the tables extracted from braids/resources.cc:
//
//   - lut_granular_envelope_rate = 2^(i/64) * (1<<14) / 8. Evaluated as
//     trunc(2048 * SemitonesToRatio(i * 12/64)) once per respawn: 0 LSB of
//     deviation across all 257 entries.
//   - lut_granular_envelope = hanning(257) * 32767, then 256 zeros. Evaluated
//     as 0.5 - 0.5 * Sine(e + 0.25) from Plaits' existing sine LUT: max
//     deviation 3.03e-5, which is 0.99 LSB of 32767, worst at index 10. The
//     256 zeros past the window are the gate `e < 1`.
//
// MEASURED. Across the seven cases in tests/ab.json: AC RMS within 0.18 dB,
// spectrum within 0.30 dB, and the two cases where an f0 exists at all read
// +0.1 and -0.3 cents once the R6 correction is applied. Every band below
// 1.3 kHz that carries 1% or more of the render's energy is inside 0.22 dB,
// with one sparse exception (scatter-full's 20-40 Hz band, 2.0% of energy,
// reads -0.41 dB). The near-empty bands are the noisiest: pitched-mid reads
// 0.4-0.5 dB low below 160 Hz, and scatter-full and stock-mid stay under
// 0.2 dB there. pitched-high used to be the worst of these -- 4.5 to 5.2 dB
// low across its 40-160 Hz bands, which hold 0.1-0.2% each of a render where
// 95% of the energy sits in one band at 320-640 Hz -- until the wav_sine DC
// pedestal below was reproduced: that low-frequency floor is exactly what a
// grain-envelope-modulated DC term generates, and reproducing it collapsed
// the error there to 0.29-0.37 dB, a 4.2-4.9 dB improvement in those three
// bands specifically (see the declared-deviations entry below for the full
// before/after). Those bands are still too small a fraction of the render to
// move the energy-weighted figure on their own. The residual that carries any
// energy at all is above 2.5 kHz, in
// bands holding 0.3-0.4% each, where the port reads up to 24 dB LOW (6 to
// 24 dB on stock-mid and scatter-full; 1 to 4 dB on the long-grain cases) --
// and that is not a decimator difference. It was traced rather than assumed:
// the reference's floor there is identical at note 12 and note 45 (0.2 dB
// apart across 33 semitones) and identical whether the reference is rendered
// at 96 kHz or decimated to 48, so it depends on neither pitch nor rate. What
// it does depend on is the envelope: Braids reads its 257-entry window with no
// interpolation, and flooring the index costs 0.0092 to 0.0118 peak and 0.0041
// to 0.0049 RMS of window amplitude across the whole grain-length range -- 46
// to 48 dB below the window peak, near enough constant -- which
// amplitude-modulates the carrier into a broadband floor. The port evaluates the window continuously and so does not
// generate it. Reproducing it would be one flooring of `position` in
// GrainEnvelope, and is deliberately not done: it is quantisation noise, not
// part of the model.
//
// OUT: the four grains summed, exactly as Braids sums them. AUX (mono): the
// cloud's amplitude on a single steady tone at the played pitch -- the same
// four envelopes, one carrier, no scatter -- so it is the grain rhythm with
// the pitch spread taken out. That AUX runs about 3.7 dB hotter than OUT at
// the stock setting because its carrier is coherent where the four grains are
// not (measured 0.305 against 0.200 RMS over three seconds), which is why the
// package declares auxGain 0.5 against outGain 0.8 -- it lands the two within
// 0.4 dB. In stereo OUT/AUX become L/R and the four grains take fixed,
// equal-power positions spread across the field, which is the stereo image the
// module could never have; the mono AUX tone is dropped. Voice.cc gives a
// stereo pair the same gain on both channels, so the asymmetric auxGain does
// not tilt the image.
//
// Declared deviations from Braids:
//   - the envelope is read as a continuous function of the phase, where Braids
//     indexes a 257-entry table with no interpolation
//     (envelope_phase >> 16, line 2049), so the port does not reproduce that
//     quantisation of the window. This is the measured HF difference above.
//   - the carrier is Plaits' interpolated sine LUT rather than Braids'
//     Interpolate824 over wav_sine, held as float rather than int16, so the
//     port does not reproduce Braids' per-sample quantisation of the grain.
//     That residual is the traced HF floor above (up to 24 dB) and is
//     deliberately not chased further -- it is quantisation noise, not part
//     of the model.
//   - wav_sine's amplitude and DC ARE reproduced (not a declared deviation,
//     noted here because the first draft of this port got it wrong twice).
//     wav_sine is not a unit -cos: fitted directly against the table
//     extracted from braids/resources.cc, it is 32638 * -cos(2*pi*i/256) +
//     127 (max residual 1 LSB of 32768) -- a -0.0343 dB level and a
//     +127/32768 DC pedestal on every sample. (An earlier hand-derived guess
//     here read 32639 from waveforms.py's scale(center=True) logic; the
//     direct fit is the one that was implemented and is authoritative.) This
//     feeds only a linear sum -- BraidsSine(g->phase) * envelope * grain gain,
//     summed over the four grains -- into a CONSTRAIN(mono, -1, 1) ceiling
//     that the reduced amplitude makes marginally LESS likely to reach, not
//     more, so there is no wavefolder/waveshaper/feedback path for the
//     pedestal to bias; this is not the port's `wav_sine is -cos, not sin`
//     quarter-cycle deviation, which stays declared and unaffected. It was
//     measured anyway, deterministically (same seed, same call sequence, so
//     the delta is the change alone, not reseed noise): reproducing the
//     amplitude and pedestal moved AC RMS by a uniform -0.034 to -0.035 dB in
//     all seven ab.json cases, exactly the amplitude's own -0.0343 dB, and
//     landed six of the seven closer to zero error (stock-mid was already
//     negative and moved 0.03 dB further, still nowhere near its 0.5 dB
//     tolerance). The pedestal's own signature -- a low-frequency term
//     windowed by the grain envelope -- shows in the near-empty low bands:
//     pitched-high's 40-160 Hz error fell from 4.5-5.2 dB low to 0.29-0.37 dB
//     (see MEASURED above). KEPT on that evidence.
//   - Braids truncates the phase increment before scaling the pitch offset
//     (`phase_increment_ >> 8`, line 2032); the port scales in float. The
//     offset differs by at most one part in 2^24 of itself.
//   - the port runs at 48 kHz against a 96 kHz original (R5 above), so grain
//     content above 24 kHz is absent rather than decimated. The A/B renders
//     the reference through the halfband decimator for this reason (R7).

#ifndef PLAITS_DSP_ENGINE2_GRANULAR_CLOUD_ENGINE_H_
#define PLAITS_DSP_ENGINE2_GRANULAR_CLOUD_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Braids keeps exactly four (digital_oscillator.h:227, `Grain grain[4]`).
const int kGranularCloudNumGrains = 4;

// The respawn loop runs once per 24-sample block at 96 kHz; this port counts
// 12 samples at 48 kHz, which is the same 250 us slot.
const int kGranularCloudSchedulerPeriod = 12;

// Braids' respawn coin: `(Random::GetWord() & 0xffff) < 0x4000`
// (digital_oscillator.cc:2026). Density opens that threshold either way; the
// top of the range is certainty, so the cloud never gaps.
const float kGranularCloudSpawnStock = 16384.0f;
const float kGranularCloudSpawnMin = 1024.0f;
const float kGranularCloudSpawnMax = 65536.0f;

// The envelope rate table is 2^(i/64) * 2048 over 257 entries, i.e. four
// octaves in 1/64-octave steps: 12/64 semitones per index.
const float kGranularCloudRateSemitonesPerStep = 12.0f / 64.0f;
const float kGranularCloudRateBase = 2048.0f;

// Braids' envelope endpoint is 1<<24. `<< 3` at 96 kHz becomes `<< 4` here.
const uint32_t kGranularCloudEnvelopeEnd = 1 << 24;
const int kGranularCloudRateShift = 4;

// Where the Hann window peaks. 0.5 is Braids' symmetric window and is what
// Shape returns at noon; the ends stop short of 0 and 1 so the window always
// has both an attack and a decay.
const float kGranularCloudPeakLate = 0.9f;
const float kGranularCloudPeakEarly = 0.1f;

// Each grain contributes a quarter of full scale, which is Braids' `>> 17` on
// a product of two 15-bit quantities (digital_oscillator.cc:2049).
const float kGranularCloudGrainGain = 0.25f;

// How wide the four grains sit in stereo. Short of hard-panned, so a single
// grain never disappears from one side.
const float kGranularCloudStereoSpread = 0.8f;

// stmlib's generator starts here (stmlib/utils/random.cc:34) and nothing else
// in this shape's path draws from it, so seeding an engine-local copy with it
// puts the port's grain schedule on the module's.
const uint32_t kGranularCloudSeed = 0x21;

class GranularCloudEngine : public Engine {
 public:
  GranularCloudEngine() { }
  ~GranularCloudEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_GRANULAR_CLOUD; }
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  virtual void HardSync() {
    for (int i = 0; i < kGranularCloudNumGrains; ++i) {
      grain_[i].phase = 0.0f;
    }
    aux_phase_ = 0.0f;
  }
#endif

 private:
  void ScheduleGrains(int timbre_code, int color_code, uint32_t spawn_threshold,
      float base_increment);
  inline uint32_t NextWord();

  struct Grain {
    float phase;
    float phase_increment;
    uint32_t envelope_phase;
    uint32_t envelope_phase_increment;
  };

  Grain grain_[kGranularCloudNumGrains];

  float aux_phase_;
  float peak_position_;

  int scheduler_countdown_;
  uint32_t rng_state_;

  DISALLOW_COPY_AND_ASSIGN(GranularCloudEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_GRANULAR_CLOUD_ENGINE_H_
