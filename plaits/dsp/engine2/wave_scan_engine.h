// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' three wavetable models -- WTBL (WAVETABLES), WMAP (WAVE_MAP) and
// WLIN (WAVE_LINE) -- in one slot, selected on HARMONICS the way z-filter
// merges the four digital filters and noise-bank merges the three noise
// models.
//
// The algorithms are Emilie Gillet's DigitalOscillator::RenderWavetables
// (digital_oscillator.cc:1525), RenderWaveMap (:1565) and RenderWaveLine
// (:1628). All three read the same 256-wave, 8-bit bank; they differ only in
// how the two knobs address it.
//
// LINEAGE, and what the Plaits neighbour cannot reach. Plaits' wavetable
// engine walks four 8x8 banks of 192 INTEGRATED 16-bit waves (kNumBanks = 4,
// kNumWavesPerBank = 64, kNumWaves = 192 -- wavetable_engine.cc:41-43; the
// file's own header comment still says "8x8x3", which is the 1.1 layout) and
// differentiates the readout. Of its 192 waves, 63 distinct ones come from
// this same Braids bank -- and only 16 of the 63 are the Braids waveform. The
// other 47 reach it only through `make_braids_family(indices)`, which takes
// the magnitude spectrum and forces every harmonic to a phase of -pi/2 before
// resynthesising (plaits/resources/wavetables.py:213-224). Measured against
// their sources, wave for wave, after removing the mean and normalising both
// to unit peak, the difference signal is LOUDER than the wave itself: over the
// 48 phase-fixed entries the median is +4.5 dB, the worst +7.0 dB and the best
// -46.1 dB. The 16 waves in the `make_braids_family(..., False)` families
// (Braids 172..187) do round-trip to the source, better than -271 dB. So the
// neighbour's bank covers 63 of the 256 waves these models need and 47 of
// those only as a different waveform, which is why this engine vendors Braids'
// own bank rather than substituting. See "TABLES" below for the byte cost.
//
// Three things follow that Plaits' wavetable engine has no control for:
//   - 20 NAMED BANKS with their own lengths and their own repeated entries.
//     Braids' wavetable_definitions (:1471) is not a uniform grid: `cello`
//     holds one wave for its top nine steps, `piano` and `drone 2` are 8 steps
//     long, `bell` is 4, and `organ` and `digital` both open on wave 176. Scanning a bank
//     is therefore not a uniform morph, and which bank you are in is a knob.
//   - A 16x16 MAP with a bilinear readout over 256 distinct waves (wt_map
//     names every one), addressed by the two knobs alone. The plane the
//     neighbour's two knobs reach is 8x8 -- a quarter of the cells -- over the
//     phase-fixed rewrites measured above. Its third axis is a genuine
//     fourth dimension rather than a finer map: HARMONICS runs z over 0..6.9999
//     across four banks and back (`if (z0 >= 4) z0 = 7 - z0`), and the readout
//     IS interpolated in z (`mix = xyz0 + (xyz1 - xyz0) * z_fractional`,
//     wavetable_engine.cc:215), hardened toward stepping only above z = 3 by
//     the `quantization` term (:130).
//   - A PHASE-DOMAIN CRUSH on the readout. WLIN's COLOR walks four zones that
//     end by mixing a 64-step sample-and-hold of the wave against a 16-step
//     one (:1716-1725) -- the wave read at 1/2 and 1/8 of the table's
//     resolution with the interpolation switched off. Plaits' wavetable AUX is
//     a 5-bit AMPLITUDE bitcrush of the output, which is a different artefact.
//
// THE BRAIDS CONTROLS, with lines. Read the parameter INDEX, not a macro
// suffix: parameter_[0] is TIMBRE and parameter_[1] is COLOR.
//   WTBL  parameter_[1] -> the BANK. `previous_parameter_[1] * 20 >> 15`
//                          (:1536), behind a +/-64-count hysteresis (:1531)
//                          that only lets the bank move when the knob does.
//         parameter_[0] -> the position WITHIN the bank.
//                          `wave_pointer = (parameter_[0] << 1) * num_steps`
//                          (:1543); the top 16 bits pick the wave pair out of
//                          wt.wave_index and the low 16 are the crossfade.
//   WMAP  parameter_[0] -> the map's X coordinate. `p = parameter_[0] * 15 >> 4`
//                          (:1575), then `coordinate = p >> 11` (:1579) and
//                          `xfade = p << 5` in uint16, i.e. (p mod 2048) * 32
//                          (:1577). X drives the OUTER Mix (:1604).
//         parameter_[1] -> the map's Y coordinate, same arithmetic (:1576,
//                          :1578, :1580). Y drives the INNER Crossfade
//                          (:1602-1603).
//   WLIN  parameter_[0] -> the position along a curated 64-wave line, through
//                          a one-pole smoother run once per block:
//                          `smoothed = (3 * smoothed + (parameter_[0] << 1))
//                          >> 2` (:1632). `scan >> 10` picks the wave, the low
//                          10 bits are the crossfade (`scan << 6`, :1639), and
//                          the PREVIOUS block's index is crossfaded in over
//                          the block as a de-zipper (:1640-1641, :1730).
//         parameter_[1] -> a four-zone crush axis, and the zones are made by
//                          OVERFLOW: `balance = parameter_[1] << 3` (:1642) is
//                          handed to Mix(int16_t, int16_t, uint16_t), so each
//                          quarter of COLOR sweeps balance 0..1 again while
//                          the enclosing if/else if chain (:1649, :1670,
//                          :1691) swaps what is being mixed. Bottom quarter:
//                          64-step S&H -> interpolated, both on the de-zipper
//                          pair. Second: the de-zipper pair -> the scanned
//                          pair. Third: interpolated -> 64-step. Top: 64-step
//                          -> 16-step.
//
//   HARMONICS  Model      WTBL / WMAP / WLIN.
//   TIMBRE     Scan       Braids' TIMBRE for the selected model.
//   MORPH      Shape      Braids' COLOR for the selected model.
//   MACRO      Snap       the scan crossfade's curve, and the only axis this
//                         engine adds. At the detent it is Braids exactly.
//                         Toward 1.0 the crossfade is pulled to 0 or 1, so the
//                         scan STEPS from whole wave to whole wave the way a
//                         PPG does instead of morphing; toward 0.0 it is
//                         pulled to 0.5, so adjacent waves sum instead of
//                         trading and the scan blurs. In WMAP it acts on both
//                         map axes at once, so at full Snap the readout is one
//                         of the cell's four corner waves and at full blur it
//                         is the average of all four. In WLIN it acts on the
//                         wave-to-wave crossfade, which Braids' own bottom
//                         quarter of MORPH does not use -- down there the scan
//                         is already locked to one wave -- so Snap does
//                         nothing in that quarter and progressively more above
//                         it. Nothing in Braids or in Plaits' wavetable engine
//                         reaches this axis: both interpolate the scan and
//                         only the scan.
//
// RATE (SPEC R5). All three functions loop `while (size--)` with no
// `size -= 2` (:1550, :1593, :1650), so all three are 96 kHz algorithms -- and
// all three are internally 2x oversampled on top of that, by the same idiom:
// `phase_increment = phase_increment_ >> 1` (:1549, :1592, :1645) added twice
// per output sample with a wave read between the two, the pair box-averaged by
// `>> 1` each (:1558-1560, :1601-1609, :1658-1668). Braids therefore reads the
// table at 192 kHz. The port runs 4x from 48 kHz to reach the same 192 kHz
// internal rate, box-averages the sub-sample pairs exactly as Braids does to
// get a 96 kHz stream, and decimates that to 48 kHz with a halfband -- which
// is Braids' own chain with the A/B harness's final decimation stage folded
// into the engine. Because the internal rate matches, every rate constant
// transfers verbatim and nothing was re-derived. The one constant that is
// expressed against the BLOCK rather than the sample rate -- WLIN's de-zipper
// increment `32768 / size` (:1641) and the `>> 2` scan smoother (:1632) --
// also transfers: Braids' block is 24 samples at 96 kHz and Plaits' is 12 at
// 48 kHz (plaits/dsp/dsp.h:51), both 4000 blocks per second.
//
// TABLES. This engine vendors Braids' wave bank, and that is the expensive
// decision in it. The 32,768 B of wave samples are stored losslessly as each
// wave's first sample followed by block-packed first and second differences:
// 23,754 B of residual payload + 770 B of first samples and offsets = 24,524 B.
// Add 256 B of wt_map + 64 B of wave_line + 360 B of definitions for 25,204 B
// of tables total.
// Four active and four staging waves plus decoder state occupy 1,152 B from
// Plaits' shared engine arena. WMAP is the widest read, at four waves, and
// decoding happens only when a requested wave is not already cached. Verified
// rather than asserted:
// the 33,024 B `wt_waves` array in braids/resources.cc is byte-identical to
// braids/data/waves.bin, which is what waveforms.py:145 reads, at 0 LSB across
// all 33,024 bytes; wt_map likewise at 0 LSB across 256. The vendored copy
// drops each wave's 129th byte -- the wrap guard Braids' Interpolate824 reads
// at index 128 -- because it equals the wave's own first sample in all 256
// waves, checked, at 0 LSB, so the port wraps the index with `& 127` instead
// and saves 256 B while reading the same value. All 256 waves are reachable
// (the 20 bank definitions alone name every one of them), so the compression
// changes storage rather than content. The generator and host test decode all
// 32,768 samples and require a byte-exact match with waves.bin.
//
// Substituting Plaits' wav_integrated_waves was costed and rejected on
// content, not on bytes: it is 50,688 B, larger than the bank being replaced,
// and it holds only 63 of the 256 waves at all, 48 of them as the phase-fixed
// rewrites measured above. WAVE_LINE alone names 45 waves that are not in it.
//
// OUT / AUX. AUX is always the complementary blend of whatever pair OUT is
// crossfading between, so it costs no extra table reads: in WTBL the two
// adjacent waves of the bank, in WMAP the opposite corner of the map cell, and
// in WLIN the other end of the crush axis -- where OUT is smooth AUX is
// crushed and back. In stereo OUT/AUX become L/R and the two sides take a
// small opposing offset of that same blend, so they are two views of one sound
// rather than two sounds.
//
// MEASURED against the module -- 24 cases in tests/ab.json, every model at
// both ends of both Braids axes plus the three sweeps that exercise the
// block-rate machinery (the bank hysteresis, the whole 16x16 map, and the scan
// smoother with its de-zipper). AC RMS agrees within 0.02 dB on every case.
// The energy-weighted spectral difference is within 0.12 dB on 23 of the 24
// and 0.24 dB on the last (WLIN at MIDI 72 inside the crush). Pitch reads
// within 0.6 cents of the corrected-sample-rate figure on every case that
// carries a cents tolerance; the two that do not are documented in
// tests/ab.json, and in both the estimator is at fault rather than the engine
// -- one signal is too harmonically rich for the autocorrelation and the other
// (the map origin, which is Braids' wave 176, a sine to within a part in 900)
// is too clean for it, and an independent spectral-peak measurement puts that
// one at -0.25 cents.
//
// Every band difference above 1 dB that carries as much as 1% of the energy is
// in the 20.5-24 kHz octave, where the halfband rolls off: -1.1 to -2.1 dB
// there on the six WLIN cases, which are the ones with real energy that high
// because the crush is a sample-and-hold. Nothing else exceeds 0.7 dB in any
// band holding 1% of the energy.
//
// GAIN (R1). The engine has no DC blocker -- it does not need one, since the
// worst mean offset over all 256 waves is 0.0042 of full scale and the median
// is 0.0001 -- and the readout itself is a convex combination of table entries
// bounded to -1.0 .. +0.992. What is NOT pinnable is the output of the
// decimator: the halfband has negative taps and an L1 norm of 1.360, so no
// analytic bound at 1.0 is available and R1 therefore requires the negative
// gains that take the limiter path. Measured peak across the seven scenarios
// is 1.0000, and worst DC 0.0038.
//
// Declared deviations from Braids:
//   - the decimator. Braids ends at 96 kHz with the 2-tap box average, which
//     the port reproduces; the 96 -> 48 kHz stage is a 19-tap Hamming halfband
//     (the same coefficient set noise-bank uses, re-derived here from its
//     windowed-sinc definition and agreeing to 2.4e-09). Against the 127-tap
//     Blackman-Harris halfband the A/B harness decimates the reference with
//     (braids/test/render_braids_model.cc, Decimate2x), it is within 0.13 dB
//     to 16 kHz, -1.41 dB at 20 kHz and -3.15 dB at 22 kHz. Its worst-case
//     alias residue is -10.4 dB at 26 kHz, falling to -38 dB by 32 kHz.
//   - Braids' `>> 1` halving of each oversampled pair floors, so it carries a
//     -0.5 LSB bias per sample; the port's box average does not. That is a DC
//     term of -1.5e-05 full scale and the A/B removes DC from both sides
//     before measuring anything.
//   - Crossfade and Mix are evaluated in float. Braids' balances are uint16
//     over 65536 and its Mix uses `(65535 - balance)`, so a "full" crossfade
//     in Braids is 65535/65536, or -0.00013 dB short of unity. The port's is
//     unity. Every quantisation that selects WHICH wave is read -- the bank
//     index, `wave_pointer >> 16`, `p >> 11`, `scan >> 10`, the `>> 2` scan
//     smoother, and the phase masks that make the 64- and 16-step crushes --
//     is done in integers exactly as Braids does it.
//   - RenderWaveLine reads `wave_line[(scan >> 10) + 1]` (:1637), and at
//     TIMBRE above 0.9844 that index is 64 on a 64-entry table. The port
//     clamps it to 63; Braids reads whatever follows the array. The A/B keeps
//     WLIN's TIMBRE at or below 0.98 for that reason and says so.
//   - TRIGGER does not reset the phase. Braids' Strike() only sets `strike_`
//     (digital_oscillator.h:284) and none of these three functions reads it,
//     so the module's phase free-runs across a trigger and the port's does
//     too.
//   - MOVING HARMONICS between the three models does not reset the phase. On
//     the module each is a separate shape, so DigitalOscillator::Render calls
//     Init() on the change (digital_oscillator.cc, the `shape_ !=
//     previous_shape_` branch) and Init() sets `phase_ = 0`
//     (digital_oscillator.h:255). Here they share one slot and one
//     accumulator, so the phase runs on -- the same choice z-filter and
//     noise-bank make, and the one that keeps a HARMONICS sweep from clicking.
//   - A wave that is not in the four-slot cache is decoded into a staging
//     buffer. One cache slot advances by one sample per 4 kHz render block,
//     round-robin, then swaps in whole. Four simultaneous WMAP misses therefore
//     fill in about 508 blocks / 127 ms. The current waves remain valid during
//     the fill. This bounds the audio-callback cost under abrupt or audio-rate
//     modulation; stationary output is byte-exact, while newly addressed waves
//     can lag.

#ifndef PLAITS_DSP_ENGINE2_WAVE_SCAN_ENGINE_H_
#define PLAITS_DSP_ENGINE2_WAVE_SCAN_ENGINE_H_

#include "stmlib/dsp/hysteresis_quantizer.h"

#include "plaits/dsp/engine/engine.h"

namespace plaits {

enum WaveScanModel {
  WAVE_SCAN_MODEL_WAVETABLES,
  WAVE_SCAN_MODEL_MAP,
  WAVE_SCAN_MODEL_LINE,
  WAVE_SCAN_MODEL_LAST
};

// 256 waves of 128 samples. Braids stores 129 and reads index 128 as the wrap
// guard; that byte equals the wave's own sample 0 in all 256 waves, so the
// port wraps with `& 127`.
const size_t kWaveScanWaveSize = 128;
const uint32_t kWaveScanWaveMask = 127;
const size_t kWaveScanNumWaves = 256;
const size_t kWaveScanCacheSize = 4;
const size_t kWaveScanCacheBuffersPerSlot = 2;
const size_t kWaveScanDecodeChunkSize = 1;
const size_t kWaveScanResidualBlockSize = 16;

struct WaveScanDecodeState {
  const uint8_t* data;
  uint8_t* destination;
  uint32_t bits;
  uint32_t num_bits;
  int32_t sample;
  int32_t difference;
  uint16_t target;
  uint16_t index;
  uint8_t bit_width;
};

// wavetable_definitions holds 20 banks; `previous_parameter_[1] * 20 >> 15`
// spans exactly 0..19 over the knob's 0..32767.
const size_t kWaveScanNumBanks = 20;

// The bank hysteresis, in Braids' 15-bit knob counts (digital_oscillator.cc
// :1531). Braids describes it as a guard against a single DAC bit flipping the
// wavetable; it also means the bank lags a slow sweep by up to 64 counts.
const int32_t kWaveScanBankHysteresis = 64;

// WMAP's grid is 16x16, so 15 interpolation squares (:1570).
const int32_t kWaveScanMapCells = 15;

// wave_line[] holds 64 entries and Braids indexes one past the scan position.
const int32_t kWaveScanLineLast = 63;

// The 96 kHz -> 48 kHz halfband needs 19 taps of history. Mirror the logical
// 32-float ring so the reverse 19-sample window is contiguous after one mask.
const size_t kWaveScanDecimatorSize = 32;
const size_t kWaveScanDecimatorStorageSize = 2 * kWaveScanDecimatorSize;
const uint32_t kWaveScanDecimatorMask = 31;

// R4: the internal stream runs 4x, so the per-sub-sample increment is clamped
// to a quarter of Plaits' 0.25-cycle-per-sample ceiling.
const float kWaveScanMaxIncrement = 0.0625f;

// How far the two stereo sides pull apart on the blend OUT and AUX share.
// Small on purpose: one step of a wavetable bank is a whole different
// waveform, so a wide split would make the two sides different sounds rather
// than a stereo image of one.
const float kWaveScanStereoSpread = 0.10f;

class WaveScanEngine : public Engine {
 public:
  WaveScanEngine() { }
  ~WaveScanEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_WAVE_SCAN; }

#if defined(TEST)
  static void TestDecodeWave(uint8_t wave, uint8_t* destination);
#endif

 private:
  void PrepareWaves(
      const uint8_t* wave_ids,
      size_t count,
      const uint8_t** waves);
  void StartDecoder(size_t slot, uint8_t wave);
  void AdvanceDecoders();

  inline uint8_t* WaveCacheBuffer(size_t slot, size_t buffer) {
    return wave_cache_ +
        (slot * kWaveScanCacheBuffersPerSlot + buffer) * kWaveScanWaveSize;
  }

  inline void Push(float main, float side) {
    const uint32_t index = decimator_write_ & kWaveScanDecimatorMask;
    decimator_main_[index] = main;
    decimator_main_[index + kWaveScanDecimatorSize] = main;
    decimator_aux_[index] = side;
    decimator_aux_[index + kWaveScanDecimatorSize] = side;
    ++decimator_write_;
  }

  float phase_;
  float frequency_;

  // Braids' previous_parameter_[1], the hysteresis-held bank knob.
  int32_t bank_parameter_;
  // Braids' smoothed_parameter_ and previous_parameter_[0], both in the same
  // 0..65534 units the module keeps them in.
  int32_t scan_;
  int32_t previous_scan_;

  stmlib::HysteresisQuantizer2 model_quantizer_;

  uint8_t* wave_cache_;
  WaveScanDecodeState* wave_decoders_;
  uint16_t wave_cache_ids_[kWaveScanCacheSize];
  uint8_t wave_cache_active_buffers_[kWaveScanCacheSize];
  uint8_t decode_slot_;

  float decimator_main_[kWaveScanDecimatorStorageSize];
  float decimator_aux_[kWaveScanDecimatorStorageSize];
  uint32_t decimator_write_;

  DISALLOW_COPY_AND_ASSIGN(WaveScanEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE2_WAVE_SCAN_ENGINE_H_
