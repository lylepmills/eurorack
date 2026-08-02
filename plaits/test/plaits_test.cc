// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

#include "plaits/dsp/dsp.h"
#include "plaits/build_config.h"
#include "plaits/pot_controller.h"

#include "plaits/dsp/chords/chord_bank.h"

#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_crossfade_engine.h"
#include "plaits/dsp/engine/virtual_analog_dual_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/parameter_randomizer.h"

#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/attractor_engine.h"
#include "plaits/dsp/engine2/gendy_engine.h"
#include "plaits/dsp/engine2/glisson_engine.h"
#include "plaits/dsp/engine2/lockstep_engine.h"
#include "plaits/dsp/engine2/loopback_engine.h"
#include "plaits/dsp/engine2/phase_flock_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/phase_weave_engine.h"
#include "plaits/dsp/engine2/reed_pipe_engine.h"
#include "plaits/dsp/engine2/rulefield_engine.h"
#include "plaits/dsp/engine2/scanned_engine.h"
#include "plaits/dsp/engine2/sideband_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/spectral_spiral_engine.h"
#include "plaits/dsp/engine2/bowed_engine.h"
#include "plaits/dsp/engine2/question_mark_engine.h"
#include "plaits/dsp/engine2/fluted_engine.h"
#include "plaits/dsp/engine2/formant_speech_engine.h"
#include "plaits/dsp/engine2/wave_paraphonic_engine.h"
#include "plaits/dsp/engine2/wave_scan_engine.h"
#include "plaits/dsp/engine2/cymbal_engine.h"
#include "plaits/dsp/engine2/snare_engine.h"
#include "plaits/dsp/engine2/kick_engine.h"
#include "plaits/dsp/engine2/lpc_speech_engine.h"
#include "plaits/dsp/engine2/struck_drum_engine.h"
#include "plaits/dsp/engine2/struck_bell_engine.h"
#include "plaits/dsp/engine2/blown_engine.h"
#include "plaits/dsp/engine2/plucked_engine.h"
#include "plaits/dsp/engine2/vosim_engine.h"
#include "plaits/dsp/engine2/harmonics_engine.h"
#include "plaits/dsp/engine2/vowel_engine.h"
#include "plaits/dsp/engine2/saw_swarm_engine.h"
#include "plaits/dsp/engine2/saw_square_engine.h"
#include "plaits/dsp/engine2/particle_burst_engine.h"
#include "plaits/dsp/engine2/noise_bank_engine.h"
#include "plaits/dsp/engine2/morph_engine.h"
#include "plaits/dsp/engine2/granular_cloud_engine.h"
#include "plaits/dsp/engine2/dual_sync_engine.h"
#include "plaits/dsp/engine2/buzz_engine.h"
#include "plaits/dsp/engine2/fold_engine.h"
#include "plaits/dsp/engine2/csaw_engine.h"
#include "plaits/dsp/engine2/ring_mod_engine.h"
#include "plaits/dsp/engine2/sub_oscillator_engine.h"
#include "plaits/dsp/engine2/toy_engine.h"
#include "plaits/dsp/engine2/digital_modulation_engine.h"
#include "plaits/dsp/engine2/saw_comb_engine.h"
#include "plaits/dsp/engine2/vowel_fof_engine.h"
#include "plaits/dsp/engine2/raw_fm_engine.h"
#include "plaits/dsp/engine2/triple_engine.h"
#include "plaits/dsp/engine2/bytebeat_engine.h"
#include "plaits/dsp/engine2/diatonic_chord_engine.h"
#include "plaits/dsp/engine2/scale_stack_engine.h"
#include "plaits/dsp/engine2/wavetable_chord_engine.h"
#include "plaits/dsp/engine2/wavetable_scale_stack_engine.h"
#include "plaits/dsp/engine2/shakers_engine.h"
#include "plaits/dsp/engine2/brass_engine.h"
#include "plaits/dsp/engine2/z_filter_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/tapfield_engine.h"
#include "plaits/dsp/engine2/undertow_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"

#include "plaits/dsp/fx/sample_rate_reducer.h"

#include "plaits/dsp/oscillator/formant_oscillator.h"
#include "plaits/dsp/oscillator/grainlet_oscillator.h"
#include "plaits/dsp/oscillator/harmonic_oscillator.h"
#include "plaits/dsp/oscillator/nes_triangle_oscillator.h"
#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/dsp/oscillator/string_synth_oscillator.h"
#include "plaits/dsp/oscillator/super_square_oscillator.h"
#include "plaits/dsp/oscillator/variable_saw_oscillator.h"
#include "plaits/dsp/oscillator/variable_shape_oscillator.h"
#include "plaits/dsp/oscillator/vosim_oscillator.h"
#include "plaits/dsp/oscillator/wavetable_oscillator.h"
#include "plaits/dsp/oscillator/z_oscillator.h"

#include "plaits/dsp/voice.h"

#include "plaits/user_data.h"
#include "plaits/user_data_receiver.h"

#include "stmlib/test/wav_writer.h"

using namespace std;
using namespace stmlib;
using namespace plaits;

const size_t kAudioBlockSize = 24;

char ram_block[16 * 1024];

void TestOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_oscillator.wav");
  
  Oscillator osc;
  osc.Init();
  
  float f = 112.0f / 48000.0f;
  
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    osc.Render<OSCILLATOR_SHAPE_SLOPE>(f, wav_writer.triangle(), out, kAudioBlockSize);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestVariableShapeOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_variable_shape_oscillator.wav");
  
  VariableShapeOscillator osc;
  osc.Init();
  
  float master_f = 110.0f / 48000.0f;
  float f = 410.0f / 48000.0f;
  
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    osc.Render(
      master_f,
      master_f * (1.0f + 4.0f * wav_writer.triangle()),
      0.5f,
      0.0f,
      out,
      kAudioBlockSize);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestVariableSawOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_variable_saw.wav");
  
  VariableSawOscillator osc;
  osc.Init();
  
  float master_f = 110.0f / 48000.0f;
  float f = 410.0f / 48000.0f;
  
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    osc.Render(
      //master_f * (1.0f + 4.0f * wav_writer.triangle()),
      62.50f / 48000.0f,
      wav_writer.triangle(),  // pw
      1.0f,  // 0 = notch , 1 = slope
      out,
      kAudioBlockSize);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestStringSynthOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_string_synth_oscillator.wav");
  
  StringSynthOscillator osc;
  osc.Init();
  
  float amplitudes[7] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
  float f = 127.5f / kSampleRate;
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    fill(&out[0], &out[kAudioBlockSize], 0.0f);
    
    osc.Render(f * (1.0f + 0.0f * wav_writer.triangle(3)), amplitudes, 1.0f, out, kAudioBlockSize);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestHarmonicOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_harmonic_oscillator.wav");
  
  HarmonicOscillator<16> osc;
  osc.Init();
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    fill(&out[0], &out[kAudioBlockSize], 0.0f);
    float f0 = 10.0f / kSampleRate;
    float amplitudes[16];
    fill(&amplitudes[0], &amplitudes[16], 0.0f);
    amplitudes[15] = 1.0f;
    osc.Render<8>(f0, amplitudes, out, kAudioBlockSize);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestWavetableOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_wavetable_oscillator.wav");
  
  #define WAVE(bank, row, column) &wav_integrated_waves[(bank * 64 + row * 8 + column) * 132]

  const int16_t* wavetable[] = {
    WAVE(2, 6, 1),
    WAVE(2, 6, 6),
    WAVE(2, 6, 4),
    WAVE(0, 6, 0),
    WAVE(0, 6, 1),
    WAVE(0, 6, 2),
    WAVE(0, 6, 7),
    WAVE(2, 4, 7),
    WAVE(2, 4, 6),
    WAVE(2, 4, 5),
    WAVE(2, 4, 4),
    WAVE(2, 4, 3),
    WAVE(2, 4, 2),
    WAVE(2, 4, 1),
    WAVE(2, 4, 0),
  };
  
  WavetableOscillator<128, 15> osc;
  osc.Init();
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    const float f0 = wav_writer.triangle(1) < 0.5f
        ? 20.0f / kSampleRate : 40.0f / kSampleRate;
    // const float f0 = wav_writer.triangle(3) * wav_writer.triangle(3) * 0.25f;
    fill(&out[0], &out[kAudioBlockSize], 0.0f);
    osc.Render(f0, 0.5f, wav_writer.triangle(7), wavetable, out, kAudioBlockSize);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestNESTriangleOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_nes_triangle_oscillator.wav");
  
  NESTriangleOscillator<> osc;
  osc.Init();
  
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    const float fm = wav_writer.triangle(10);
    const float f0 = i < (3 * kSampleRate)
        ? 107.0f / kSampleRate
        : i < (5 * kSampleRate)
            ? 853.12f / kSampleRate
            : fm * fm * fm * fm * 0.5f;
    osc.Render(f0, out, kAudioBlockSize);
    for (size_t j = 0; j < kAudioBlockSize; ++j) {
      out[j] *= 0.8f;
    }
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestSuperSquareOscillator() {
  WavWriter wav_writer(1, kSampleRate, 10);
  wav_writer.Open("plaits_supersquare_oscillator.wav");
  
  SuperSquareOscillator osc;
  osc.Init();
  
  for (size_t i = 0; i < kSampleRate * 10; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    const float f0 = 110.0f / kSampleRate;
    const float shape = wav_writer.triangle(10);
    osc.Render(f0, shape, out, kAudioBlockSize);
    for (size_t j = 0; j < kAudioBlockSize; ++j) {
      out[j] *= 0.8f;
    }
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestFormantOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_formant_oscillator.wav");
  
  FormantOscillator osc;
  osc.Init();
  
  float fm = 239.7f / 48000.0f;
  float fs = 105.0f / 48000.0f;
  
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float modulation = 1.0f + 4.0f * wav_writer.triangle();
    osc.Render(fm, fs * modulation, 0.75f, out, kAudioBlockSize);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestVosimOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_vosim_oscillator.wav");
  
  VOSIMOscillator osc;
  osc.Init();
  
  float f0 = 105.0f / 48000.0f;
  float f1 = 1390.7f / 48000.0f;
  float f2 = 817.2f / 48000.0f;
  
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float modulation = wav_writer.triangle();
    osc.Render(f0, f1 * (1.0f + modulation), f2, modulation, out, kAudioBlockSize);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestZOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_z_oscillator.wav");
  
  ZOscillator osc;
  osc.Init();
  
  float f0 = 80.0f / 48000.0f;
  float f1 = 250.0f / 48000.0f;
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float modulation = wav_writer.triangle(7);
    float modulation_2 = wav_writer.triangle(11);
    osc.Render(f0, f1 * (1.0f + modulation * 8.0f), modulation_2, 0.5f, out, kAudioBlockSize);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestGrainletOscillator() {
  WavWriter wav_writer(1, kSampleRate, 20);
  wav_writer.Open("plaits_grainlet_oscillator.wav");
  
  GrainletOscillator osc;
  osc.Init();
  
  float f0 = 80.0f / 48000.0f;
  float f1 = 2000.0f / 48000.0f;
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float modulation = wav_writer.triangle(7) * 0.0f;
    float modulation_2 = wav_writer.triangle(11) * 0.0f;
    float modulation_3 = wav_writer.triangle(13);
    osc.Render(f0, f1 * (1.0f + modulation * 8.0f), modulation_3, 1.0f, out, kAudioBlockSize);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void TestAdditiveEngine() {
  WavWriter wav_writer(2, kSampleRate, 60);
  wav_writer.Open("plaits_additive_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  AdditiveEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 36.0f;

  for (size_t i = 0; i < kSampleRate * 60; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.morph = wav_writer.triangle(13) * 0.0f + 0.7f;
    p.timbre = wav_writer.triangle(7) * 1.0f;
    p.harmonics = wav_writer.triangle(5) * 0.5f + 0.5f;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestChordEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_chord_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  ChordEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 48.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.harmonics = wav_writer.triangle(17) * 1.0f;
    p.morph = wav_writer.triangle(11) * 1.0f;
    p.timbre = /*wav_writer.triangle(13) * 1.0f*/ 0.5f;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestFMEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_fm_engine.wav");
  
  FMEngine e;
  e.Init(NULL);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 48.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.timbre = wav_writer.triangle(11);
    p.harmonics = /*wav_writer.triangle(14)*/ 0.75f;
    p.morph = /*1.0f - wav_writer.triangle(19)*/ 0.0f;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestGrainEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_grain_engine.wav");
  
  GrainEngine e;
  e.Init(NULL);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 110.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.note = /*84.0f + Random::GetFloat() * 0.1f + wav_writer.triangle(2) * 12.0f*/ 36.0f;
    p.timbre = wav_writer.triangle(7);
    p.morph = wav_writer.triangle(11);
    p.harmonics = wav_writer.triangle(19);
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestModalEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_modal_engine.wav");
  
  ModalEngine e;
  e.Init(NULL);
  e.Reset();
  
  EngineParameters p;
  p.accent = 0.0f;
  p.note = 36.0f;
  bool flip_flop = false;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.trigger = TRIGGER_LOW;
    if (i % (kAudioBlockSize * 2000) == 0) {
      flip_flop = !flip_flop;
      p.note = flip_flop ? 48.0f : 55.0f;
      p.trigger = TRIGGER_RISING_EDGE;
      p.accent = 1.0f;
    }
    p.timbre = wav_writer.triangle(17);
    p.harmonics = 0.25f;
    p.morph = wav_writer.triangle(7);
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestNoiseEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_noise_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  NoiseEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.note = 84.0f;
    p.timbre = 0.0f;
    p.morph = 0.5f;
    p.harmonics = 0.0f * wav_writer.triangle(3);
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestParticleEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_particle_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  ParticleEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.note = 96.0f;
  p.trigger = TRIGGER_LOW;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.timbre = /*wav_writer.triangle(17)*/0.5f;
    p.harmonics = /*0.5f*/ 0.7f;
    p.morph = /*0.0f*/0.7f;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestSpeechEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_speech_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  SpeechEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_UNPATCHED;
  p.accent = 0.8f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.timbre = wav_writer.triangle(11) * 0.0f + 0.5f;
    p.harmonics = wav_writer.triangle(17) * 0.45f;
    p.note = 48.0f + wav_writer.triangle(1) * 0.0f;
    p.morph = wav_writer.triangle(7);
    // p.trigger = TRIGGER_LOW;
    // if (i % (kAudioBlockSize * 3000) == 0) {
    //   p.trigger = TRIGGER_RISING_EDGE;
    // }
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void GenerateStringTuningData() {
  for (int pass = 0; pass < 21; ++pass) {
    WavWriter wav_writer(1, kSampleRate, 4);
    
    char file_name[80];
    sprintf(file_name, "string_%02d.wav", pass);
    wav_writer.Open(file_name);
    
    BufferAllocator allocator(ram_block, 16384);
    StringEngine e;
    e.Init(&allocator);
    e.Reset();
    
    EngineParameters p;
    p.accent = 0.5f;
    p.note = 72.0f;
    for (size_t i = 0; i < kSampleRate * 4; i += kAudioBlockSize) {
      float out[kAudioBlockSize];
      float aux[kAudioBlockSize];
      p.trigger = i == 0 ? TRIGGER_RISING_EDGE : TRIGGER_LOW;
      p.timbre = 0.8f;
      p.morph = 0.8f;
      p.harmonics = float(pass) / 20.0f;
      bool already_enveloped;
      e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
      wav_writer.Write(out, kAudioBlockSize);
    }
  }
  
  WavWriter wav_writer(1, kSampleRate, 40);
  wav_writer.Open("string_sweep.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  StringEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.accent = 0.2f;
  p.note = 36.0f;
  p.timbre = 0.8f;
  p.morph = 0.8f;
  p.trigger = TRIGGER_UNPATCHED;
  for (size_t i = 0; i < kSampleRate * 40; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.harmonics = wav_writer.triangle(7);
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, kAudioBlockSize);
  }
}

void GenerateModalTuningData() {
  for (int pass = 0; pass < 21; ++pass) {
    WavWriter wav_writer(1, kSampleRate, 4);
    
    char file_name[80];
    sprintf(file_name, "modal_%02d.wav", pass);
    wav_writer.Open(file_name);
    
    BufferAllocator allocator(ram_block, 16384);
    ModalEngine e;
    e.Init(&allocator);
    e.Reset();
    
    EngineParameters p;
    p.accent = 0.5f;
    p.note = 48.0f;
    for (size_t i = 0; i < kSampleRate * 4; i += kAudioBlockSize) {
      float out[kAudioBlockSize];
      float aux[kAudioBlockSize];
      p.trigger = i == (kAudioBlockSize * 1000)
          ? TRIGGER_RISING_EDGE : TRIGGER_LOW;
      p.timbre = 0.5f;
      p.morph = 0.8f;
      p.harmonics = float(pass) / 20.0f;
      bool already_enveloped;
      e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
      wav_writer.Write(out, kAudioBlockSize);
    }
  }
}

void TestStringEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_string_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  StringEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.accent = 0.0f;
  p.note = 36.0f;
  int note = 0;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.trigger = TRIGGER_LOW;
    if (i % (kAudioBlockSize * 2000) == 0) {
      note = (note + 1) % 3;
      float notes[3] = { 48.0f, 55.0f, 36.0f };
      p.note = notes[note];
      p.trigger = TRIGGER_RISING_EDGE;
      p.accent = 0.0f;
    }
    p.timbre = 0.7f;
    p.harmonics = 0.9f;
    p.morph = 0.7f;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestSwarmEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_swarm_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  SwarmEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_UNPATCHED;
  
  Limiter out_limiter;
  Limiter aux_limiter;
  
  out_limiter.Init();
  aux_limiter.Init();

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.timbre = wav_writer.triangle(33) * 0.0f + 0.5f;
    p.harmonics = 0.3f;
    p.morph = wav_writer.triangle(17);
    p.note = 48.0f;
    //p.trigger = TRIGGER_LOW;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    
    out_limiter.Process(2.0f, out, kAudioBlockSize);
    aux_limiter.Process(0.8f, aux, kAudioBlockSize);
    
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestVirtualAnalogEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_virtual_analog_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  VirtualAnalogEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 48.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.timbre = wav_writer.triangle(7);
    p.harmonics = wav_writer.triangle(11);
    p.morph = 1.0f - wav_writer.triangle(5);
    // p.timbre = wav_writer.triangle(3) * 0.0f + 0.0f;
    // p.harmonics = wav_writer.triangle(19);
    // p.morph = wav_writer.triangle(19) * 0.0f + 0.3f;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestPhaseDistortionEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_phase_distortion_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  PhaseDistortionEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 36.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.timbre = wav_writer.triangle(5);
    p.harmonics = wav_writer.triangle(11);
    p.morph = wav_writer.triangle(19);
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestVirtualAnalogVCFEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_virtual_analog_vcf_engine.wav");
  
  VirtualAnalogVCFEngine e;
  e.Init(NULL);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 48.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    float out2[kAudioBlockSize];
    p.timbre = wav_writer.triangle(31);
    p.harmonics = wav_writer.triangle(17);
    p.morph = wav_writer.triangle(7);
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestStringMachineEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_string_machine_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  StringMachineEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 48.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.timbre = 1.0f;
    p.harmonics = 0.33f;
    p.morph = wav_writer.triangle(7);
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestChiptuneEngine() {
  WavWriter wav_writer(2, kSampleRate, 100);
  wav_writer.Open("plaits_chiptune_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  ChiptuneEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.note = 48.0f;

  for (size_t i = 0; i < kSampleRate * 100; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.morph = wav_writer.triangle(7);
    p.harmonics = wav_writer.triangle(59);
    p.timbre = wav_writer.triangle(31);
    
    p.trigger = i > kSampleRate * 60
        ? TRIGGER_UNPATCHED
        : (i % size_t(kSampleRate / 8) == 0
              ? TRIGGER_RISING_EDGE
              : TRIGGER_LOW);
    
    e.set_envelope_shape(p.trigger == TRIGGER_UNPATCHED
        ? ChiptuneEngine::NO_ENVELOPE
        : 2.0f * wav_writer.triangle(23) - 1.0f);
    
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestWaveshapingEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_waveshaping_engine.wav");
  
  WaveshapingEngine e;
  e.Init(NULL);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 48.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.timbre = 0.1f + 0.9f * wav_writer.triangle(7);
    p.harmonics = 0.0f + 1.0f * wav_writer.triangle(11);
    p.morph = 0.0f + 1.0f * wav_writer.triangle(5);
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestWavetableEngine() {
  WavWriter wav_writer(2, kSampleRate, 5);
  wav_writer.Open("plaits_wavetable_engine.wav");
  
  WavetableEngine e;
  BufferAllocator allocator(ram_block, 16384);
  e.Init(&allocator);
  e.Reset();
  e.LoadUserData(NULL);
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 24.0f;

  for (size_t i = 0; i < kSampleRate * 5; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    float phi = wav_writer.triangle(1);
    p.timbre = /*phi > 0.9f ? 0.0f : 0.5f + 0.5f * sinf(phi * 24.3f)*/ phi;
    p.harmonics = wav_writer.triangle(11) * 0 + 0.0f;
    p.morph = wav_writer.triangle(5) * 0;
    
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestWaveTerrainEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_wave_terrain_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  WaveTerrainEngine e;
  e.Init(&allocator);
  e.Reset();
  
  int8_t custom_terrain[4096];
  for (int x = 0; x < 64; ++x) {
    for (int y = 0; y < 64; ++y) {
      custom_terrain[x + 64 * y] = 127.0f * sinf((x * y) / 300.0f);
    }
  }
  e.LoadUserData((uint8_t*)(custom_terrain));
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  p.note = 36.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.timbre = wav_writer.triangle(7);
    p.harmonics = wav_writer.triangle(37);
    p.morph = wav_writer.triangle(19);
    
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void EnumerateWavetables() {
  WavWriter wav_writer(1, kSampleRate, 64);
  wav_writer.Open("plaits_wavetable_enumeration.wav");
  
  WavetableEngine e;
  e.Init(NULL);
  e.Reset();
  
  EngineParameters p;
  p.trigger = TRIGGER_LOW;
  
  int bank = 0;
  int division = 4;
  bool swap = true;
  
  for (int d = 0; d < division; ++d) {
    for (int column = 0; column < 8; ++column) {
      for (int row = 0; row < 8; ++row) {
        for (size_t i = 0; i < kSampleRate/ division; i += kAudioBlockSize) {
          float out[kAudioBlockSize];
          float aux[kAudioBlockSize];
          p.note = 36.0f;
          p.timbre = (swap ? column : row) / 7.0f;
          p.harmonics = bank / 2.0f;
          p.morph = (swap ? row : column) / 7.0f;
          bool already_enveloped;
          e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
          wav_writer.Write(out, kAudioBlockSize);
        }
      }
    }
  }
}

void TestSampleRateReducer() {
  WavWriter wav_writer(2, kSampleRate, 20);
  wav_writer.Open("plaits_sample_rate_reducer.wav");
  
  SampleRateReducer src;
  SineOscillator osc;
  osc.Init();
  src.Init();
  
  float f0 = 100.0f / 48000.0f;
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float in[kAudioBlockSize];
    float fx[kAudioBlockSize];
    fill(&in[0], &in[kAudioBlockSize], 0.0f);
    float f = 110 / kSampleRate;
    float a = 1.0f;
    osc.Render(f, a, in, kAudioBlockSize);
    copy(&in[0], &in[kAudioBlockSize], &fx[0]);
    src.Process<true>(0.1666f + 0.8333f * wav_writer.triangle(7), fx, kAudioBlockSize);
    wav_writer.Write(in, fx, kAudioBlockSize);
  } 
}

void TestBassDrumEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_bass_drum_engine.wav");
  
  BassDrumEngine e;
  e.Init(NULL);
  e.Reset();
  
  EngineParameters p;
  p.accent = 0.0f;
  p.note = 33.4f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.trigger = TRIGGER_LOW;
    if (i % (kAudioBlockSize * 1000) == 0) {
      p.trigger = TRIGGER_RISING_EDGE;
      p.accent = 1.0f;
    }
    p.timbre = wav_writer.triangle(5) * 1.0f + 0.0f;
    p.harmonics = wav_writer.triangle(7) * 1.0f + 0.0f;
    p.morph = wav_writer.triangle(17) * 1.0f + 0.0f;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestSnareDrumEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_snare_drum_engine.wav");
  
  SnareDrumEngine e;
  e.Init(NULL);
  e.Reset();
  
  EngineParameters p;
  p.accent = 0.0f;
  p.note = 51.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.trigger = TRIGGER_LOW;
    if (i % (kAudioBlockSize * 1000) == 0) {
      p.trigger = TRIGGER_RISING_EDGE;
      p.accent = 1.0f;
    }
    p.timbre = wav_writer.triangle(5);
    p.harmonics = wav_writer.triangle(7);
    p.morph = wav_writer.triangle(17);
    // p.timbre = 0.5f;
    // p.harmonics = 0.5f;
    // p.morph = 0.0f;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestHiHatEngine() {
  WavWriter wav_writer(2, kSampleRate, 80);
  wav_writer.Open("plaits_hi_hat_engine.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  HiHatEngine e;
  e.Init(&allocator);
  e.Reset();
  
  EngineParameters p;
  p.accent = 0.0f;

  for (size_t i = 0; i < kSampleRate * 80; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.trigger = TRIGGER_LOW;
    if (i % (kAudioBlockSize * 250) == 0) {
      p.trigger = TRIGGER_RISING_EDGE;
      p.accent = 1.0f;
    }
    p.note = 48.0f + wav_writer.triangle(11) * 36.0f;
    p.timbre = wav_writer.triangle(17);
    p.harmonics = wav_writer.triangle(7);
    p.morph = /*wav_writer.triangle(3)*/ 0.5f;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

void TestVoice() {
  WavWriter wav_writer(2, kSampleRate, 200);
  wav_writer.Open("plaits_voice.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  Voice v;
  
  v.Init(&allocator);
  
  Patch patch;
  Modulations modulations;
  
  patch.engine = 9;
  patch.note = 48.0f;
  patch.harmonics = 0.3f;
  patch.timbre = 0.7f;
  patch.morph = 0.7f;
  patch.frequency_modulation_amount = 0.0f;
  patch.timbre_modulation_amount = 0.0f;
  patch.morph_modulation_amount = 0.0f;
  patch.decay = 0.1f;
  patch.lpg_colour = 0.0f;
  
  modulations.note = 0.0f;
  modulations.engine = 0.0f;
  modulations.frequency = 0.0f;
  modulations.note = 0.0f;
  modulations.harmonics = 0.0f;
  modulations.morph = 0.0;
  modulations.level = 1.0f;
  modulations.trigger = 0.0f;
  modulations.frequency_patched = false;
  modulations.timbre_patched = false;
  modulations.morph_patched = false;
  modulations.trigger_patched = true;
  modulations.level_patched = false;
  
  for (size_t i = 0; i < kSampleRate * 200; i += kAudioBlockSize) {
    modulations.trigger = (i % (kAudioBlockSize * 500) <= kAudioBlockSize * 5) ? 1.0f : 0.0f;
    // modulations.level = 1.0f;
    Voice::Frame frames[kAudioBlockSize];
    v.Render(patch, modulations, frames, kAudioBlockSize);
    wav_writer.WriteFrames(&frames[0].out, kAudioBlockSize);
  }
}

void TestFMGlitch() {
  WavWriter wav_writer(2, kSampleRate, 200);
  wav_writer.Open("plaits_fm_glitch.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  Voice v;

  v.Init(&allocator);
  
  Patch patch;
  Modulations modulations;
  
  patch.engine = 12;
  patch.note = 48.0f;
  patch.harmonics = 0.5f;
  patch.timbre = 0.5f;
  patch.morph = 0.5f;
  patch.frequency_modulation_amount = 0.0f;
  patch.timbre_modulation_amount = 0.0f;
  patch.morph_modulation_amount = 0.0f;
  patch.decay = 0.1f;
  patch.lpg_colour = 0.0f;
  
  modulations.note = 0.0f;
  modulations.engine = 0.0f;
  modulations.frequency = 0.0f;
  modulations.note = 0.0f;
  modulations.harmonics = 0.0f;
  modulations.morph = 0.0;
  modulations.level = 1.0f;
  modulations.trigger = 0.0f;
  modulations.frequency_patched = true;
  modulations.timbre_patched = false;
  modulations.morph_patched = false;
  modulations.trigger_patched = false;
  modulations.level_patched = false;
  
  for (size_t i = 0; i < kSampleRate * 200; i += kAudioBlockSize) {
    Voice::Frame frames[kAudioBlockSize];
    v.Render(patch, modulations, frames, kAudioBlockSize);
    wav_writer.WriteFrames(&frames[0].out, kAudioBlockSize);
    modulations.frequency = frames[0].out;
    patch.frequency_modulation_amount = wav_writer.triangle(11) * 1.0f;
  }
}

void TestLPGAttackDecay() {
  WavWriter wav_writer(2, kSampleRate, 20);
  wav_writer.Open("plaits_lpg_attack_decay.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  Voice v;

  v.Init(&allocator);
  
  Patch patch;
  Modulations modulations;
  
  patch.engine = 9;
  patch.note = 48.0f;
  patch.harmonics = 0.5f;
  patch.timbre = 0.0f;
  patch.morph = 0.0f;
  patch.frequency_modulation_amount = 0.8f;
  patch.timbre_modulation_amount = 0.0f;
  patch.morph_modulation_amount = 0.0f;
  patch.decay = 0.1f;
  patch.lpg_colour = 0.5f;
  
  modulations.note = 0.0f;
  modulations.engine = 0.0f;
  modulations.frequency = 0.0f;
  modulations.note = 0.0f;
  modulations.harmonics = 0.0f;
  modulations.morph = 0.0;
  modulations.level = 1.0f;
  modulations.trigger = 0.0f;
  modulations.frequency_patched = false;
  modulations.timbre_patched = false;
  modulations.morph_patched = false;
  modulations.trigger_patched = true;
  modulations.level_patched = false;
    
  for (size_t i = 0; i < kSampleRate * 20; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];

    modulations.trigger = 0.0f;
    if (i % (kAudioBlockSize * 1000) == 0) {
      patch.note += 1.0f;
      modulations.trigger = 1.0f;
    }
    modulations.level = (i % (1000 * kAudioBlockSize)) < 100 * kAudioBlockSize ? 1.0f : 0.0f;
    
    Voice::Frame frames[kAudioBlockSize];
    v.Render(patch, modulations, frames, kAudioBlockSize);
    wav_writer.WriteFrames(&frames[0].out, kAudioBlockSize);
  }
}

void TestLimiterGlitch() {
  WavWriter wav_writer(2, kSampleRate, 50);
  wav_writer.Open("plaits_limiter_glitch.wav");
  
  BufferAllocator allocator(ram_block, 16384);
  Voice v;

  v.Init(&allocator);
  
  Patch patch;
  Modulations modulations;
  
  patch.engine = 17;
  patch.note = 36.0f;
  patch.harmonics = 0.8f;
  patch.timbre = 0.6f;
  patch.morph = 0.4f;
  patch.frequency_modulation_amount = 0.0f;
  patch.timbre_modulation_amount = 0.0f;
  patch.morph_modulation_amount = 0.0f;
  patch.decay = 0.1f;
  patch.lpg_colour = 0.0f;
  
  modulations.note = 0.0f;
  modulations.frequency = 0.0f;
  modulations.note = 0.0f;
  modulations.harmonics = 0.0f;
  modulations.morph = 0.0;
  modulations.level = 1.0f;
  modulations.frequency_patched = false;
  modulations.timbre_patched = false;
  modulations.morph_patched = false;
  modulations.trigger_patched = true;
  modulations.level_patched = false;
  
  for (size_t i = 0; i < kSampleRate * 50; i += kAudioBlockSize) {
    Voice::Frame frames[kAudioBlockSize];
    v.Render(patch, modulations, frames, kAudioBlockSize);
    wav_writer.WriteFrames(&frames[0].out, kAudioBlockSize);
    modulations.trigger = i % (100 * kAudioBlockSize) == 0 ? 1.0f : 0.0f;
    modulations.engine = wav_writer.triangle(12) * 0.2f;
    patch.frequency_modulation_amount = wav_writer.triangle(3) * 1.0f;
  }
}

template<typename T>
void RenderExperimentalEngine(const char* name) {
  WavWriter wav_writer(2, kSampleRate, 12);
  wav_writer.Open(name);

  BufferAllocator allocator(ram_block, 16384);
  T e;
  e.Init(&allocator);
  e.Reset();

  EngineParameters p;
  p.note = 48.0f;
  p.accent = 0.8f;
  p.chord_set_option = 0;

  for (size_t i = 0; i < kSampleRate * 12; i += kAudioBlockSize) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.trigger = i < kSampleRate * 6 ? TRIGGER_UNPATCHED : TRIGGER_LOW;
    if (i >= kSampleRate * 6 &&
        i % static_cast<size_t>(kSampleRate * 2) == 0) {
      p.trigger = TRIGGER_RISING_EDGE | TRIGGER_HIGH;
    }
    p.harmonics = wav_writer.triangle(11);
    p.timbre = wav_writer.triangle(7);
    p.morph = wav_writer.triangle(5);
    p.macro = wav_writer.triangle(9);

    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    for (size_t j = 0; j < kAudioBlockSize; ++j) {
      if (!isfinite(out[j]) || !isfinite(aux[j]) ||
          fabsf(out[j]) > 4.0f || fabsf(aux[j]) > 4.0f) {
        fprintf(
            stderr,
            "%s failed at sample %zu: h=%f t=%f m=%f macro=%f out=%f aux=%f\n",
            name,
            i + j,
            p.harmonics,
            p.timbre,
            p.morph,
            p.macro,
            out[j],
            aux[j]);
        abort();
      }
    }
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
}

// A steady offset on a tap is inaudible on a development machine and invisible
// to the finite/bounds and control-response gates, but it reaches a module as a
// fat DC shift with the tone buried in it. Brass measured OUT at RMS 27923
// against a DC component of 26548 before its taps were blocked, and passed both
// of the other gates. The threshold matches the SDK's per-scenario check in
// plaits_lab.py (abs(mean) <= 0.2 over a signal that runs to about +/-1), so a
// built-in engine and a packaged one are held to the same standard.
const float kMaxAuditionDcOffset = 0.2f;

int audition_dc_failures = 0;

void CheckAuditionDcOffset(
    const char* name, double out_sum, double aux_sum, size_t frames) {
  const float out_dc = static_cast<float>(out_sum / static_cast<double>(frames));
  const float aux_dc = static_cast<float>(aux_sum / static_cast<double>(frames));
  const bool out_bad = fabsf(out_dc) > kMaxAuditionDcOffset;
  const bool aux_bad = fabsf(aux_dc) > kMaxAuditionDcOffset;
  printf(
      "  %-28s OUT DC %+.5f   AUX DC %+.5f%s\n",
      name,
      out_dc,
      aux_dc,
      (out_bad || aux_bad) ? "   <-- EXCESSIVE" : "");
  fflush(stdout);
  if (out_bad || aux_bad) {
    fprintf(
        stderr,
        "%s has excessive DC offset (OUT %+.5f, AUX %+.5f; limit %.2f) — "
        "center each tap around zero, typically with a DC blocker on the tap "
        "rather than by trimming the signal that feeds it\n",
        name,
        out_dc,
        aux_dc,
        kMaxAuditionDcOffset);
    ++audition_dc_failures;
  }
}

// Reported after every audition render rather than at the first offender, so
// one run shows the whole picture instead of hiding later engines behind an
// early abort.
void ReportAuditionDcFailures() {
  if (audition_dc_failures) {
    fprintf(
        stderr,
        "%d audition render(s) exceeded the DC limit — see above\n",
        audition_dc_failures);
    abort();
  }
}

// A listening render with one clearly isolated control sweep per four-second
// segment. MAIN is the left channel and AUX is the right channel.
template<typename T>
void RenderAuditionEngine(const char* name) {
  const size_t kSegmentFrames = static_cast<size_t>(kSampleRate * 4.0f);
  const size_t kTotalFrames = kSegmentFrames * 4;
  WavWriter wav_writer(2, kSampleRate, 16);
  wav_writer.Open(name);

  BufferAllocator allocator(ram_block, sizeof(ram_block));
  T e;
  e.Init(&allocator);
  e.LoadUserData(NULL);
  e.Reset();

  EngineParameters p;
  p.note = 48.0f;
  p.accent = 0.8f;
  p.chord_set_option = 0;

  double out_sum = 0.0;
  double aux_sum = 0.0;
  size_t summed_frames = 0;

  for (size_t frame = 0; frame < kTotalFrames; frame += kAudioBlockSize) {
    const size_t segment = frame / kSegmentFrames;
    const size_t segment_frame = frame % kSegmentFrames;
    const float sweep = 0.02f + 0.96f * static_cast<float>(segment_frame) / \
        static_cast<float>(kSegmentFrames - 1);
    p.harmonics = 0.5f;
    p.timbre = 0.5f;
    p.morph = 0.5f;
    p.macro = 0.5f;
    if (segment == 0) {
      p.harmonics = sweep;
    } else if (segment == 1) {
      p.timbre = sweep;
    } else if (segment == 2) {
      p.morph = sweep;
    } else {
      p.macro = sweep;
    }
    p.trigger = segment_frame < kAudioBlockSize
        ? TRIGGER_RISING_EDGE | TRIGGER_HIGH
        : TRIGGER_UNPATCHED;

    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    bool already_enveloped = false;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      if (!isfinite(out[i]) || !isfinite(aux[i]) ||
          fabsf(out[i]) > 4.0f || fabsf(aux[i]) > 4.0f) {
        fprintf(
            stderr,
            "%s audition failed at frame %zu: segment=%zu "
            "h=%f t=%f m=%f macro=%f out=%f aux=%f\n",
            name,
            frame + i,
            segment,
            p.harmonics,
            p.timbre,
            p.morph,
            p.macro,
            out[i],
            aux[i]);
        abort();
      }
      out_sum += out[i];
      aux_sum += aux[i];
    }
    summed_frames += kAudioBlockSize;
    wav_writer.Write(out, aux, kAudioBlockSize);
  }
  CheckAuditionDcOffset(name, out_sum, aux_sum, summed_frames);
}

template<typename T>
void ValidateExperimentalControlResponse(const char* name, bool stereo = false) {
  static char allocator_memory_a[16 * 1024];
  static char allocator_memory_b[16 * 1024];
  const char* control_names[] = { "HARMONICS", "TIMBRE", "MORPH", "macro" };

  for (int control = 0; control < 4; ++control) {
    memset(allocator_memory_a, 0, sizeof(allocator_memory_a));
    memset(allocator_memory_b, 0, sizeof(allocator_memory_b));
    BufferAllocator allocator_a(allocator_memory_a, sizeof(allocator_memory_a));
    BufferAllocator allocator_b(allocator_memory_b, sizeof(allocator_memory_b));
    T low;
    T high;
    low.Init(&allocator_a);
    high.Init(&allocator_b);
    low.LoadUserData(NULL);
    high.LoadUserData(NULL);
    low.Reset();
    high.Reset();

    EngineParameters low_parameters;
    low_parameters.note = 48.0f;
    low_parameters.accent = 0.8f;
    low_parameters.chord_set_option = 0;
    low_parameters.harmonics = 0.43f;
    low_parameters.timbre = 0.57f;
    low_parameters.morph = 0.39f;
    low_parameters.macro = 0.61f;
    low_parameters.stereo = stereo;
    EngineParameters high_parameters = low_parameters;
    float* low_control = control == 0 ? &low_parameters.harmonics
        : control == 1 ? &low_parameters.timbre
        : control == 2 ? &low_parameters.morph
        : &low_parameters.macro;
    float* high_control = control == 0 ? &high_parameters.harmonics
        : control == 1 ? &high_parameters.timbre
        : control == 2 ? &high_parameters.morph
        : &high_parameters.macro;
    *low_control = 0.12f;
    *high_control = 0.88f;

    double difference = 0.0;
    double reference = 0.0;
    for (size_t block = 0; block < 1024; ++block) {
      low_parameters.trigger = high_parameters.trigger = block == 0
          ? TRIGGER_RISING_EDGE | TRIGGER_HIGH
          : TRIGGER_UNPATCHED;
      float low_out[kAudioBlockSize];
      float low_aux[kAudioBlockSize];
      float high_out[kAudioBlockSize];
      float high_aux[kAudioBlockSize];
      bool already_enveloped = false;
      low.Render(
          low_parameters,
          low_out,
          low_aux,
          kAudioBlockSize,
          &already_enveloped);
      high.Render(
          high_parameters,
          high_out,
          high_aux,
          kAudioBlockSize,
          &already_enveloped);
      for (size_t i = 0; i < kAudioBlockSize; ++i) {
        difference += fabsf(low_out[i] - high_out[i]);
        difference += 0.61803398875f * fabsf(low_aux[i] - high_aux[i]);
        reference += fabsf(low_out[i]) + fabsf(high_out[i]);
        reference += 0.61803398875f * (fabsf(low_aux[i]) + fabsf(high_aux[i]));
      }
    }
    const double threshold = max(0.01, reference * 0.0001);
    if (difference < threshold) {
      fprintf(
          stderr,
          "%s does not respond to %s: difference=%f threshold=%f\n",
          name,
          control_names[control],
          difference,
          threshold);
      abort();
    }
  }
}

template<typename T>
void ValidateExperimentalEngineExtremes(
    float maximum = 4.0f,
    bool stereo = false) {
  BufferAllocator allocator(ram_block, 16384);
  // Static storage mirrors the firmware's zero-initialized Voice instance.
  // Several original engines rely on that initialization before Init().
  static T e;
  e.Init(&allocator);
  e.LoadUserData(NULL);

  const float values[] = { 0.0f, 0.5f, 1.0f };
  for (size_t h = 0; h < 3; ++h) {
    for (size_t t = 0; t < 3; ++t) {
      for (size_t m = 0; m < 3; ++m) {
        for (size_t x = 0; x < 3; ++x) {
          e.Reset();
          EngineParameters p;
          p.note = 60.0f;
          p.accent = 1.0f;
          p.chord_set_option = 0;
          p.harmonics = values[h];
          p.timbre = values[t];
          p.morph = values[m];
          p.macro = values[x];
          p.stereo = stereo;
          for (size_t block = 0; block < 64; ++block) {
            float out[kAudioBlockSize];
            float aux[kAudioBlockSize];
            p.trigger = block == 0
                ? TRIGGER_RISING_EDGE | TRIGGER_HIGH
                : TRIGGER_UNPATCHED;
            bool already_enveloped;
            e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
            for (size_t i = 0; i < kAudioBlockSize; ++i) {
              if (!isfinite(out[i]) || !isfinite(aux[i]) ||
                  fabsf(out[i]) > maximum || fabsf(aux[i]) > maximum) {
                fprintf(
                    stderr,
                    "Extreme failed: h=%f t=%f m=%f macro=%f block=%zu "
                    "sample=%zu out=%f aux=%f\n",
                    p.harmonics,
                    p.timbre,
                    p.morph,
                    p.macro,
                    block,
                    i,
                    out[i],
                    aux[i]);
                abort();
              }
            }
          }
        }
      }
    }
  }
}

template<typename T>
double StockMacroSignature(float macro) {
  BufferAllocator allocator(ram_block, 16384);
  Random::Seed(0x21);
  static T e;
  e.Init(&allocator);
  e.LoadUserData(NULL);
  e.Reset();

  EngineParameters p;
  p.note = 48.0f;
  p.accent = 0.8f;
  p.chord_set_option = 0;
  p.harmonics = 0.57f;
  p.timbre = 0.73f;
  p.morph = 0.41f;
  p.macro = macro;

  double signature = 0.0;
  for (size_t block = 0; block < 512; ++block) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.trigger = block == 0
        ? TRIGGER_RISING_EDGE | TRIGGER_HIGH
        : TRIGGER_LOW;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      if (!isfinite(out[i]) || !isfinite(aux[i])) {
        abort();
      }
      signature += fabsf(out[i]) + 0.61803398875f * fabsf(aux[i]);
    }
  }
  return signature;
}

template<typename T>
void ValidateStockMacroResponse(const char* name) {
  const double low = StockMacroSignature<T>(0.0f);
  const double stock = StockMacroSignature<T>(0.5f);
  const double high = StockMacroSignature<T>(1.0f);
  const double threshold = max(1.0, stock * 0.0001);
  if (fabs(low - stock) < threshold && fabs(high - stock) < threshold) {
    fprintf(
        stderr,
        "%s fourth macro is inaudible: low=%f stock=%f high=%f\n",
        name,
        low,
        stock,
        high);
    abort();
  }
}

void ValidateStockMacroMidpoint() {
  const float stock_values[] = { 0.0f, 0.17f, 0.5f, 0.83f, 1.0f };
  for (size_t i = 0; i < 5; ++i) {
    const float stock = stock_values[i];
    if (ApplyMacro(stock, 0.0f, 1.0f, 0.5f) != stock) {
      fprintf(stderr, "Fourth macro midpoint changed stock value %f\n", stock);
      abort();
    }
  }
}

void PrepareSixOpTestBank(uint8_t* bank) {
  memset(bank, 0, UserData::SIZE);
  for (int patch = 0; patch < 32; ++patch) {
    uint8_t* data = bank + patch * fm::Patch::SYX_SIZE;
    for (int op = 0; op < 6; ++op) {
      uint8_t* op_data = data + op * 17;
      for (int i = 0; i < 8; ++i) {
        op_data[i] = 99;
      }
      op_data[12] = 7 << 3;
      op_data[14] = 99;
      op_data[15] = 1 << 1;
    }
    for (int i = 0; i < 4; ++i) {
      data[102 + i] = 99;
      data[106 + i] = 50;
    }
    data[110] = 0;
    data[111] = 3 | (1 << 3);
    data[117] = 24;
  }
}

double SixOpMacroSignature(
    const uint8_t* bank,
    float macro,
    float harmonics,
    size_t num_blocks = 512) {
  memset(ram_block, 0, sizeof(ram_block));
  BufferAllocator allocator(ram_block, sizeof(ram_block));
  static SixOpEngine e;
  e.Init(&allocator);
  e.LoadUserData(bank);

  EngineParameters p;
  p.note = 48.0f;
  p.accent = 0.8f;
  p.chord_set_option = 0;
  p.harmonics = harmonics;
  p.timbre = 0.5f;
  p.morph = 0.5f;
  p.macro = macro;

  double signature = 0.0;
  for (size_t block = 0; block < num_blocks; ++block) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.trigger = TRIGGER_UNPATCHED;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      if (!isfinite(out[i]) || !isfinite(aux[i]) ||
          fabsf(out[i]) > 16.0f || fabsf(aux[i]) > 16.0f) {
        abort();
      }
      signature += fabsf(out[i]);
    }
  }
  return signature;
}

void ValidateSixOpMacroResponse() {
  static uint8_t bank[UserData::SIZE];
  PrepareSixOpTestBank(bank);
  const double low = SixOpMacroSignature(bank, 0.0f, 0.0f);
  const double stock = SixOpMacroSignature(bank, 0.5f, 0.0f);
  const double high = SixOpMacroSignature(bank, 1.0f, 0.0f);
  const double threshold = max(1.0, stock * 0.0001);
  if (fabs(low - stock) < threshold && fabs(high - stock) < threshold) {
    fprintf(
        stderr,
        "Six-op FM fourth macro is inaudible: low=%f stock=%f high=%f\n",
        low,
        stock,
        high);
    abort();
  }

  double factory_signatures[3];
  for (int bank_index = 0; bank_index < 3; ++bank_index) {
    double bank_stock = 0.0;
    for (int patch = 0; patch < 32; ++patch) {
      const float harmonics = (
          static_cast<float>(patch) + 0.5f) / (32.0f * 1.02f);
      const double patch_low = SixOpMacroSignature(
          fm_patches_table[bank_index], 0.0f, harmonics, 128);
      const double patch_stock = SixOpMacroSignature(
          fm_patches_table[bank_index], 0.5f, harmonics, 128);
      const double patch_high = SixOpMacroSignature(
          fm_patches_table[bank_index], 1.0f, harmonics, 128);
      // Quiet factory patches can have a whole-render signature below 1.0.
      // An absolute floor of 1.0 labels even a several-fold MACRO response as
      // unchanged; retain the intended 0.1% relative comparison with only a
      // numerical-noise floor.
      const double patch_threshold = max(1.0e-6, patch_stock * 0.001);
      if (fabs(patch_low - patch_stock) < patch_threshold && \
          fabs(patch_high - patch_stock) < patch_threshold) {
        fprintf(
            stderr,
            "Factory DX bank %d patch %d ignores the fourth macro: "
            "low=%f stock=%f high=%f\n",
            bank_index,
            patch,
            patch_low,
            patch_stock,
            patch_high);
        abort();
      }
      bank_stock += patch_stock;
    }
    factory_signatures[bank_index] = bank_stock;
  }

  for (int a = 0; a < 3; ++a) {
    for (int b = a + 1; b < 3; ++b) {
      const double difference = fabs(
          factory_signatures[a] - factory_signatures[b]);
      const double threshold = max(
          1.0, min(factory_signatures[a], factory_signatures[b]) * 0.001);
      if (difference < threshold) {
        fprintf(stderr, "Factory DX banks %d and %d are indistinguishable\n", a, b);
        abort();
      }
    }
  }
}

// Fill `count` patches with a distinct, audible carrier output level each, and
// zero the rest of the 32-slot blob. Distinct levels make each patch produce a
// measurably different render signature; the zeroed tail is silent — so a
// Harmonics position that lands on it (which the old fixed-32 quantizer did for
// most of the dial when count < 32) reads as ~0.
void PrepareDistinctSixOpBank(uint8_t* bank, int count) {
  memset(bank, 0, UserData::SIZE);
  for (int patch = 0; patch < count; ++patch) {
    uint8_t* data = bank + patch * fm::Patch::SYX_SIZE;
    for (int op = 0; op < 6; ++op) {
      uint8_t* op_data = data + op * 17;
      for (int i = 0; i < 8; ++i) {
        op_data[i] = 99;
      }
      op_data[12] = 7 << 3;
      // Carrier output level, spread across patches so each is distinct and all
      // stay clearly audible (99, 91, 83, ... never near-silent). Spacing 8
      // clears the nonlinear DX7 level->loudness curve near the top so adjacent
      // patches resolve as separate plateaus.
      op_data[14] = static_cast<uint8_t>(99 - 8 * patch);
      op_data[15] = 1 << 1;
    }
    for (int i = 0; i < 4; ++i) {
      data[102 + i] = 99;
      data[106 + i] = 50;
    }
    data[110] = 0;
    data[111] = 3 | (1 << 3);
    data[117] = 24;
  }
}

double SixOpMacroSignatureLen(
    const uint8_t* bank,
    size_t length,
    float harmonics,
    size_t num_blocks = 128) {
  memset(ram_block, 0, sizeof(ram_block));
  BufferAllocator allocator(ram_block, sizeof(ram_block));
  static SixOpEngine e;
  e.Init(&allocator);
  e.LoadUserData(bank, length);

  EngineParameters p;
  p.note = 48.0f;
  p.accent = 0.8f;
  p.chord_set_option = 0;
  p.harmonics = harmonics;
  p.timbre = 0.5f;
  p.morph = 0.5f;
  p.macro = 0.5f;

  double signature = 0.0;
  for (size_t block = 0; block < num_blocks; ++block) {
    float out[kAudioBlockSize];
    float aux[kAudioBlockSize];
    p.trigger = TRIGGER_UNPATCHED;
    bool already_enveloped;
    e.Render(p, out, aux, kAudioBlockSize, &already_enveloped);
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      if (!isfinite(out[i]) || !isfinite(aux[i]) ||
          fabsf(out[i]) > 16.0f || fabsf(aux[i]) > 16.0f) {
        abort();
      }
      signature += fabsf(out[i]);
    }
  }
  return signature;
}

// A bank baked with fewer than 32 patches must map the WHOLE Harmonics range
// across exactly those patches — the reason the web builder no longer has to
// zone-fill a short pick list up to 32. Two claims:
//   1) No Harmonics position selects an empty (silent) slot: the minimum
//      signature over a fine sweep stays clearly audible. The old fixed-32
//      quantizer would send most of the dial into the zeroed tail (~0).
//   2) The sweep still reaches every real patch: the number of distinct
//      signature plateaus equals the bank's patch count.
void ValidateSixOpShortBank() {
  static uint8_t bank[UserData::SIZE];
  const int kCount = 5;
  PrepareDistinctSixOpBank(bank, kCount);
  const size_t length = kCount * fm::Patch::SYX_SIZE;

  const int kSteps = 40;
  double min_sig = 1e30;
  double plateaus[64];
  int num_plateaus = 0;
  for (int step = 0; step < kSteps; ++step) {
    const float harmonics = (static_cast<float>(step) + 0.5f) / kSteps;
    const double sig = SixOpMacroSignatureLen(bank, length, harmonics);
    if (sig < min_sig) {
      min_sig = sig;
    }
    bool matched = false;
    for (int j = 0; j < num_plateaus; ++j) {
      if (fabs(sig - plateaus[j]) < max(1.0, plateaus[j] * 0.02)) {
        matched = true;
        break;
      }
    }
    if (!matched && num_plateaus < 64) {
      plateaus[num_plateaus++] = sig;
    }
  }

  // Claim 1: the whole dial stays on real (audible) patches. A full-bank stock
  // patch here signs near 100 or above with the single-voice drone renderer;
  // an empty slot signs ~0. Use a floor well above zero but far below a real
  // patch rather than coupling the reachability test to renderer scheduling.
  if (min_sig < 10.0) {
    fprintf(
        stderr,
        "Short six-op bank (%d patches) leaves a silent Harmonics zone: "
        "min signature over the sweep = %f (expected all positions audible)\n",
        kCount,
        min_sig);
    abort();
  }

  // Claim 2: every baked patch is reachable, and no more than that exist.
  if (num_plateaus != kCount) {
    fprintf(
        stderr,
        "Short six-op bank (%d patches) maps Harmonics to %d distinct patches "
        "(expected %d)\n",
        kCount,
        num_plateaus,
        kCount);
    abort();
  }
}

// A region bank: patches [0, split) carry `low` carrier level, [split, count)
// carry `high`, and the 32-slot tail is zeroed (silent). This lets a switch test
// read the resident bank's SIZE off the audio: whether the top of the dial is
// silent (past the real patches) or loud (reaching a high-index loud patch).
void PrepareRegionSixOpBank(
    uint8_t* bank, int count, int split, uint8_t low, uint8_t high) {
  memset(bank, 0, UserData::SIZE);
  for (int patch = 0; patch < count; ++patch) {
    uint8_t* data = bank + patch * fm::Patch::SYX_SIZE;
    const uint8_t level = patch < split ? low : high;
    for (int op = 0; op < 6; ++op) {
      uint8_t* op_data = data + op * 17;
      for (int i = 0; i < 8; ++i) {
        op_data[i] = 99;
      }
      op_data[12] = 7 << 3;
      op_data[14] = level;
      op_data[15] = 1 << 1;
    }
    for (int i = 0; i < 4; ++i) {
      data[102 + i] = 99;
      data[106 + i] = 50;
    }
    data[110] = 0;
    data[111] = 3 | (1 << 3);
    data[117] = 24;
  }
}

// Sweep the Harmonics dial across an ALREADY-LOADED engine; report the quietest
// and loudest position. No Init/Reset here — the caller owns the engine, which
// is what lets this probe a bank SWITCH on one shared instance.
void SixOpSweepMinMax(SixOpEngine* e, double* out_min, double* out_max) {
  const int kSteps = 40;
  double min_sig = 1e30, max_sig = 0.0;
  for (int step = 0; step < kSteps; ++step) {
    EngineParameters p;
    p.note = 48.0f;
    p.accent = 0.8f;
    p.chord_set_option = 0;
    p.harmonics = (static_cast<float>(step) + 0.5f) / kSteps;
    p.timbre = 0.5f;
    p.morph = 0.5f;
    p.macro = 0.5f;
    double sig = 0.0;
    for (size_t block = 0; block < 128; ++block) {
      float out[kAudioBlockSize];
      float aux[kAudioBlockSize];
      p.trigger = TRIGGER_UNPATCHED;
      bool already_enveloped;
      e->Render(p, out, aux, kAudioBlockSize, &already_enveloped);
      for (size_t i = 0; i < kAudioBlockSize; ++i) {
        if (!isfinite(out[i]) || fabsf(out[i]) > 16.0f) {
          abort();
        }
        sig += fabsf(out[i]);
      }
    }
    if (sig < min_sig) min_sig = sig;
    if (sig > max_sig) max_sig = sig;
  }
  *out_min = min_sig;
  *out_max = max_sig;
}

// The factory DX7 banks are a SINGLE SixOpEngine instance RegisterInstance'd at
// three slots; voice.cc swaps the resident bank (LoadUserData + Reset) when you
// navigate between those slots. So banks of different lengths never coexist in
// the engine — but the SHARED patch-index quantizer must re-size on EVERY switch.
// Drive that switch on one instance and read the resident bank's size off the
// audio. Two banks, chosen so a mis-size is unambiguous:
//   SMALL: 6 patches, all LOUD, slots 6..31 silent.
//   LARGE: 16 patches; low indices QUIET, high indices (>=6) LOUD.
// Facts that only hold if the quantizer re-sizes on each switch:
//   - after switching to SMALL: no Harmonics position is silent (a stale larger
//     quantizer would dip into SMALL's empty 6..N tail -> min ~ 0).
//   - after switching to LARGE: the dial reaches a LOUD high-index patch (a stale
//     smaller quantizer would only reach LARGE's quiet low indices -> max stays
//     quiet).
// Reset() after each load mirrors voice.cc's post-load Reset on a slot switch.
void ValidateSixOpBankSwitch() {
  static uint8_t bank_small[UserData::SIZE];
  static uint8_t bank_large[UserData::SIZE];
  const int kSmall = 6;
  const int kLarge = 16;
  PrepareRegionSixOpBank(bank_small, kSmall, kSmall, 99, 99);   // all loud
  PrepareRegionSixOpBank(bank_large, kLarge, kSmall, 20, 99);   // low quiet, high loud

  // Loud vs quiet render signatures, measured on fresh instances, to derive
  // thresholds the switch sequence must satisfy.
  double dummy_min, loud_max, quiet_min, quiet_max;
  {
    memset(ram_block, 0, sizeof(ram_block));
    BufferAllocator a(ram_block, sizeof(ram_block));
    static SixOpEngine ref;
    ref.Init(&a);
    ref.LoadUserData(bank_small, kSmall * fm::Patch::SYX_SIZE);
    ref.Reset();
    SixOpSweepMinMax(&ref, &dummy_min, &loud_max);          // all-loud reference
  }
  {
    memset(ram_block, 0, sizeof(ram_block));
    BufferAllocator a(ram_block, sizeof(ram_block));
    static SixOpEngine ref;
    ref.Init(&a);
    // A "quiet-only" bank = LARGE's low region, to bound what a stuck-small
    // quantizer could ever reach.
    static uint8_t quiet[UserData::SIZE];
    PrepareRegionSixOpBank(quiet, kSmall, kSmall, 20, 20);
    ref.LoadUserData(quiet, kSmall * fm::Patch::SYX_SIZE);
    ref.Reset();
    SixOpSweepMinMax(&ref, &quiet_min, &quiet_max);
  }
  // Sanity: loud is clearly louder than quiet, with a wide margin to test in.
  const double loud_floor = 0.5 * (quiet_max + loud_max);

  memset(ram_block, 0, sizeof(ram_block));
  BufferAllocator allocator(ram_block, sizeof(ram_block));
  static SixOpEngine e;
  e.Init(&allocator);

  struct { const uint8_t* bank; int count; bool loud_top; } seq[] = {
    { bank_small, kSmall, false },  // fresh
    { bank_large, kLarge, true  },  // grow  (6 -> 16): must reach a loud high patch
    { bank_small, kSmall, false },  // shrink (16 -> 6): must have no silent zone
    { bank_large, kLarge, true  },  // grow again
  };

  for (int s = 0; s < 4; ++s) {
    e.LoadUserData(seq[s].bank, seq[s].count * fm::Patch::SYX_SIZE);
    e.Reset();  // mirror voice.cc's post-load Reset on a slot switch
    double min_sig = 0.0, max_sig = 0.0;
    SixOpSweepMinMax(&e, &min_sig, &max_sig);

    // Shrink integrity: the resident bank has no silent tail in range.
    if (min_sig < quiet_min * 0.5) {
      fprintf(stderr,
          "Bank switch step %d (%d patches): a Harmonics position is silent "
          "(min=%f) — quantizer did not shrink to the resident bank\n",
          s, seq[s].count, min_sig);
      abort();
    }
    // Grow integrity: switching up must reach a loud high-index patch that a
    // stuck-smaller quantizer could never address.
    if (seq[s].loud_top && max_sig < loud_floor) {
      fprintf(stderr,
          "Bank switch step %d (%d patches): dial never reached a loud "
          "high-index patch (max=%f < %f) — quantizer did not re-grow\n",
          s, seq[s].count, max_sig, loud_floor);
      abort();
    }
  }
}

void ValidateScaleVoiceBank() {
  int microtonal_scale = -1;
  int microtonal_degree = -1;
  for (int scale = 0; scale < kScaleVoicesNumScales; ++scale) {
    const Scale& definition = kScaleVoicesScales[scale];
    if (definition.num_degrees < 1 ||
        definition.num_degrees > kScaleVoicesMaxDegrees) {
      fprintf(stderr, "Scale %d has an invalid degree count\n", scale);
      abort();
    }
    if (definition.pitches[0] != 0) {
      fprintf(stderr, "Scale %d does not start at its root\n", scale);
      abort();
    }
    float previous = ScaleDegreeToNote(0, scale);
    for (int degree = 1; degree < definition.num_degrees; ++degree) {
      const float note = ScaleDegreeToNote(degree, scale);
      if (!isfinite(note) || note <= previous || note >= 12.0f) {
        fprintf(stderr, "Scale %d has an invalid degree at %d\n", scale, degree);
        abort();
      }
      if (microtonal_scale < 0
          && definition.pitches[degree] % kScaleVoicesUnitsPerSemitone != 0) {
        microtonal_scale = scale;
        microtonal_degree = degree;
      }
      previous = note;
    }
    if (fabsf(ScaleDegreeToNote(definition.num_degrees, scale) - 12.0f) >
        1e-6f) {
      fprintf(stderr, "Scale %d does not wrap at one octave\n", scale);
      abort();
    }
  }

  // If this recipe carries any microtonal pitch, pin the first one through both
  // conversion paths so a later "simplification" back to integer semitones
  // cannot silently erase its precision. The ordinary eight-scale fallback is
  // all 12-TET, so it legitimately has no candidate here.
  if (microtonal_scale >= 0) {
    const float microtonal_note =
        static_cast<float>(
            kScaleVoicesScales[microtonal_scale].pitches[microtonal_degree]) /
        static_cast<float>(kScaleVoicesUnitsPerSemitone);
    if (fabsf(
            ScaleDegreeToNote(microtonal_degree, microtonal_scale)
            - microtonal_note) > 1e-6f) {
      fprintf(stderr, "A scale lost its 1/128-semitone tuning\n");
      abort();
    }
    float residual = 0.0f;
    if (QuantizeToScale(microtonal_note, microtonal_scale, &residual)
            != microtonal_degree
        || fabsf(residual) > 1e-6f) {
      fprintf(stderr, "The scale quantizer does not preserve a microtonal degree\n");
      abort();
    }
  }
}

int PeakFrameAmplitude(const Voice::Frame* frames, size_t size) {
  int peak = 0;
  for (size_t i = 0; i < size; ++i) {
    peak = max(peak, abs(static_cast<int>(frames[i].out)));
    peak = max(peak, abs(static_cast<int>(frames[i].aux)));
  }
  return peak;
}

void ValidateDynamicHiddenPotRouting() {
  float main = 0.5f;
  float default_hidden = 0.3f;
  float temporary_hidden = 0.0f;
  PotController pot;
  pot.Init(&main, &default_hidden, 1.0f, 1.0f, 0.0f);

  pot.ProcessControlRate(0.5f);
  pot.ProcessUIRate();
  pot.Lock(&temporary_hidden);
  pot.ProcessControlRate(0.75f);
  pot.ProcessUIRate();
  if (!pot.editing_hidden_parameter() ||
      fabsf(main - 0.5f) > 0.001f ||
      fabsf(default_hidden - 0.3f) > 0.001f ||
      fabsf(temporary_hidden - 0.75f) > 0.001f) {
    fprintf(stderr,
        "Temporary hidden pot routing changed the wrong parameter\n");
    abort();
  }

  // Releasing after an edited gesture must restore the knob's ordinary hidden
  // target.
  pot.Unlock();
  pot.Lock();
  pot.ProcessControlRate(1.0f);
  pot.ProcessUIRate();
  if (!pot.editing_hidden_parameter() ||
      fabsf(default_hidden - 1.0f) > 0.001f ||
      fabsf(temporary_hidden - 0.75f) > 0.001f) {
    fprintf(stderr,
        "Temporary hidden pot routing did not restore the default target\n");
    abort();
  }

  // A short right-button press takes the Realign path through Navigate.
  pot.Realign();
  pot.ProcessControlRate(0.25f);
  if (fabsf(main - 0.25f) > 0.001f) {
    fprintf(stderr,
        "Temporary hidden pot routing did not restore normal tracking\n");
    abort();
  }
}

void ValidateClockedChiptuneLevelVca() {
  BufferAllocator allocator(ram_block, sizeof(ram_block));
  Voice voice;
  voice.Init(&allocator);

  Patch patch;
  memset(&patch, 0, sizeof(patch));
  patch.engine = 7;  // Chiptune in both generated and stock registries.
  patch.note = 48.0f;
  patch.harmonics = 0.4f;
  patch.timbre = 0.5f;
  patch.morph = 0.5f;
  patch.decay = 0.5f;
  patch.lpg_colour = 0.5f;
  patch.level_cv_option = 0;

  Modulations modulations;
  memset(&modulations, 0, sizeof(modulations));
  modulations.trigger = 1.0f;
  modulations.trigger_patched = true;
  modulations.level_patched = true;
  // Keep Chiptune's optional internal TIMBRE envelope out of this test: LEVEL
  // must be a VCA whether that envelope is enabled or not.
  modulations.timbre_patched = true;
  modulations.level = 1.0f;

  Voice::Frame frames[kAudioBlockSize];
  int audible_peak = 0;
  // The firmware deliberately delays trigger recognition by five blocks.
  for (int block = 0; block < 12; ++block) {
    voice.Render(patch, modulations, frames, kAudioBlockSize);
    audible_peak = max(
        audible_peak, PeakFrameAmplitude(frames, kAudioBlockSize));
  }
  if (audible_peak < 100) {
    fprintf(stderr,
        "Clocked Chiptune LEVEL VCA test never produced an audible signal\n");
    abort();
  }

  modulations.level = 0.0f;
  voice.Render(patch, modulations, frames, kAudioBlockSize);
  if (PeakFrameAmplitude(frames, kAudioBlockSize) > 1) {
    fprintf(stderr,
        "Clocked Chiptune ignored LEVEL CV in the explicit Level mode\n");
    abort();
  }

  // Auto must classify clocked Chiptune as self-enveloped and preserve the
  // same LEVEL VCA instead of stealing the input for decay.
  patch.level_cv_option = 2;
  modulations.level = 1.0f;
  voice.Render(patch, modulations, frames, kAudioBlockSize);
  if (PeakFrameAmplitude(frames, kAudioBlockSize) < 100) {
    fprintf(stderr,
        "Clocked Chiptune Auto LEVEL mode did not restore its audible signal\n");
    abort();
  }
  modulations.level = 0.0f;
  voice.Render(patch, modulations, frames, kAudioBlockSize);
  if (PeakFrameAmplitude(frames, kAudioBlockSize) > 1) {
    fprintf(stderr,
        "Clocked Chiptune Auto LEVEL mode did not preserve the VCA\n");
    abort();
  }
}

void ValidateAutoLevelDecayRouting() {
  BufferAllocator allocator(ram_block, sizeof(ram_block));
  Voice voice;
  voice.Init(&allocator);

  Patch patch;
  memset(&patch, 0, sizeof(patch));
  patch.engine = 8;  // Virtual Analog, an ordinary outer-LPG engine.
  patch.note = 48.0f;
  patch.harmonics = 0.4f;
  patch.timbre = 0.5f;
  patch.morph = 0.5f;
  patch.decay = 0.5f;
  patch.lpg_colour = 0.5f;
  patch.level_cv_option = 2;

  Modulations modulations;
  memset(&modulations, 0, sizeof(modulations));
  modulations.trigger = 1.0f;
  modulations.trigger_patched = true;
  modulations.level_patched = true;
  modulations.level = 0.0f;

  Voice::Frame frames[kAudioBlockSize];
  int audible_peak = 0;
  for (int block = 0; block < 12; ++block) {
    voice.Render(patch, modulations, frames, kAudioBlockSize);
    audible_peak = max(
        audible_peak, PeakFrameAmplitude(frames, kAudioBlockSize));
  }
  if (audible_peak < 100) {
    fprintf(stderr,
        "Auto LEVEL mode did not route an ordinary oscillator to LPG decay\n");
    abort();
  }
}

template<typename E>
void RenderSpeechEquivalenceSequence(
    E* engine,
    EngineParameters parameters,
    float* out,
    float* aux,
    bool* enveloped) {
  const size_t blocks = 16;
  for (size_t block = 0; block < blocks; ++block) {
    parameters.trigger = block == 0 ? TRIGGER_RISING_EDGE : TRIGGER_LOW;
    engine->Render(
        parameters,
        out + block * kMaxBlockSize,
        aux + block * kMaxBlockSize,
        kMaxBlockSize,
        &enveloped[block]);
  }
}

void AssertSpeechEquivalence(
    const char* label,
    const float* expected_out,
    const float* expected_aux,
    const bool* expected_enveloped,
    const float* actual_out,
    const float* actual_aux,
    const bool* actual_enveloped) {
  const size_t blocks = 16;
  const size_t samples = blocks * kMaxBlockSize;
  for (size_t i = 0; i < samples; ++i) {
    if (expected_out[i] != actual_out[i] ||
        expected_aux[i] != actual_aux[i]) {
      fprintf(stderr, "%s diverged from stock Speech at sample %zu\n", label, i);
      abort();
    }
  }
  for (size_t block = 0; block < blocks; ++block) {
    if (expected_enveloped[block] != actual_enveloped[block]) {
      fprintf(stderr, "%s changed Speech's envelope declaration at block %zu\n",
          label, block);
      abort();
    }
  }
}

void ValidateSpeechEngineSplit() {
  const size_t blocks = 16;
  const size_t samples = blocks * kMaxBlockSize;
  float stock_out[samples];
  float stock_aux[samples];
  float split_out[samples];
  float split_aux[samples];
  bool stock_enveloped[blocks];
  bool split_enveloped[blocks];

  // The full-range model axis is stock Speech's group 0..2 stretched across
  // the dial: naive -> SAM -> LPC phonemes. Intermediate points pin both stock
  // crossfades as well as their three exact anchors.
  const float models[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
  const float colours[] = { 0.2f, 0.5f, 0.8f };
  for (size_t m = 0; m < 5; ++m) {
    for (size_t c = 0; c < 3; ++c) {
      for (size_t stereo = 0; stereo < 2; ++stereo) {
        uint8_t stock_ram[16384] = { 0 };
        uint8_t split_ram[16384] = { 0 };
        BufferAllocator stock_allocator(stock_ram, sizeof(stock_ram));
        BufferAllocator split_allocator(split_ram, sizeof(split_ram));
        SpeechEngine stock;
        FormantSpeechEngine split;
        stock.Init(&stock_allocator);
        split.Init(&split_allocator);

        EngineParameters stock_parameters;
        stock_parameters.note = 60.0f;
        stock_parameters.harmonics = models[m] / 3.0f;
        stock_parameters.timbre = 0.63f;
        stock_parameters.morph = 0.37f;
        stock_parameters.macro = colours[c];
        stock_parameters.accent = 0.8f;
        stock_parameters.chord_set_option = 0;
        stock_parameters.stereo = stereo != 0;
        EngineParameters split_parameters = stock_parameters;
        split_parameters.harmonics = models[m];

        Random::Seed(0x51ee);
        RenderSpeechEquivalenceSequence(
            &stock, stock_parameters, stock_out, stock_aux, stock_enveloped);
        Random::Seed(0x51ee);
        RenderSpeechEquivalenceSequence(
            &split, split_parameters, split_out, split_aux, split_enveloped);
        AssertSpeechEquivalence(
            "Speech Sounds", stock_out, stock_aux, stock_enveloped,
            split_out, split_aux, split_enveloped);
      }
    }
  }

  // LPC Words removes the phoneme position and stretches the five word banks
  // across HARMONICS. TIMBRE and MORPH preserve stock Speech's vocal-tract and
  // word-address controls, while MACRO exposes the speed that stock Speech
  // hides on its attenuverter. The explicit prosody setter is the endpoint of
  // Voice's inherited FM-attenuverter routing.
  const float speeds[] = { -0.6f, 0.0f, 0.6f };
  const float prosodies[] = { -1.0f, 0.0f, 0.7f };
  for (int material = 0; material < 5; ++material) {
    for (size_t stereo = 0; stereo < 2; ++stereo) {
      for (size_t s = 0; s < 3; ++s) {
        for (size_t p = 0; p < 3; ++p) {
          uint8_t stock_ram[16384] = { 0 };
          uint8_t split_ram[16384] = { 0 };
          BufferAllocator stock_allocator(stock_ram, sizeof(stock_ram));
          BufferAllocator split_allocator(split_ram, sizeof(split_ram));
          SpeechEngine stock;
          LPCSpeechEngine split;
          stock.Init(&stock_allocator);
          split.Init(&split_allocator);
          stock.set_prosody_amount(prosodies[p]);
          split.set_prosody_amount(prosodies[p]);

          const float stock_quantizer_value =
              static_cast<float>(material + 1) / 6.0f + 1.0f / 12.0f;
          const float split_bank = static_cast<float>(material) / 4.0f;
          const float split_speed = 0.5f + speeds[s] * 0.5f;
          // Derive the stock speed through the new control's actual arithmetic
          // so bit-exact output comparisons do not mistake float round-off for
          // a synthesis difference.
          stock.set_speed((split_speed - 0.5f) * 2.0f);
          EngineParameters stock_parameters;
          stock_parameters.note = 60.0f;
          stock_parameters.harmonics =
              (2.0f + stock_quantizer_value / 0.275f) / 6.0f;
          stock_parameters.timbre = 0.64f;
          stock_parameters.morph = 0.38f;
          stock_parameters.macro = 0.5f;
          stock_parameters.accent = 0.8f;
          stock_parameters.chord_set_option = 0;
          stock_parameters.stereo = stereo != 0;
          EngineParameters split_parameters = stock_parameters;
          split_parameters.harmonics = split_bank;
          split_parameters.timbre = stock_parameters.timbre;
          split_parameters.morph = stock_parameters.morph;
          split_parameters.macro = split_speed;

          Random::Seed(0x1ec10);
          RenderSpeechEquivalenceSequence(
              &stock, stock_parameters, stock_out, stock_aux, stock_enveloped);
          Random::Seed(0x1ec10);
          RenderSpeechEquivalenceSequence(
              &split, split_parameters, split_out, split_aux, split_enveloped);
          AssertSpeechEquivalence(
              "LPC Words", stock_out, stock_aux, stock_enveloped,
              split_out, split_aux, split_enveloped);
        }
      }
    }
  }
}

void ValidateLPCDiscreteFrameBounds() {
  // This address/formant combination deterministically selects consonant 10,
  // the final frame in LPCSpeechSynthController::phonemes_. Under ASan the
  // old discrete PlayFrame path failed here because it also dereferenced frame
  // 15 just to apply a zero blend.
  LPCSpeechSynthController controller;
  controller.Init(NULL);
  float excitation[kAudioBlockSize];
  float output[kAudioBlockSize];
  controller.Render(
      false,
      true,
      -1,
      0.0f,
      0.0f,
      1.0f,
      0.125f,
      1.0f / 3.0f,
      1.0f,
      excitation,
      output,
      kAudioBlockSize);
}

void ValidateOneKnobEnvelope() {
  // The compact tables must remain perceptually transparent relative to the
  // original Elements formulas.
  float max_quartic_error = 0.0f;
  float max_exponential_error = 0.0f;
  float max_rate_relative_error = 0.0f;
  const float normalization = 1.0f - expf(-4.0f);
  const float control_rate = kSampleRate / static_cast<float>(kBlockSize);
  const float min_increment = 1.0f / (8.0f * control_rate);
  const float max_increment = 1.0f / (0.0005f * control_rate);
  const float gamma = 0.175f;
  const float a = powf(max_increment, -gamma);
  const float b = powf(min_increment, -gamma);
  for (int i = 0; i <= 10000; ++i) {
    const float t = static_cast<float>(i) / 10000.0f;
    const float expected_quartic = powf(t, 3.32f);
    const float expected_exponential =
        (1.0f - expf(-4.0f * t)) / normalization;
    const float expected_rate =
        powf(a + (b - a) * t, -1.0f / gamma);
    max_quartic_error = max(
        max_quartic_error,
        fabsf(OneKnobEnvelope::TestQuarticCurve(t) - expected_quartic));
    max_exponential_error = max(
        max_exponential_error,
        fabsf(
            OneKnobEnvelope::TestExponentialCurve(t) -
            expected_exponential));
    max_rate_relative_error = max(
        max_rate_relative_error,
        fabsf(OneKnobEnvelope::TestTimeIncrement(t) / expected_rate - 1.0f));
  }
  if (max_quartic_error > 0.00025f ||
      max_exponential_error > 0.00050f ||
      max_rate_relative_error > 0.0017f) {
    fprintf(
        stderr,
        "One-knob envelope table error: quartic=%f exponential=%f rate=%f\n",
        max_quartic_error,
        max_exponential_error,
        max_rate_relative_error);
    abort();
  }

  // A narrow trigger at the pluck end must complete the one-shot; treating
  // Plaits' usual 1 ms trigger as an ADSR gate would cut the attack off.
  OneKnobEnvelope envelope;
  envelope.Init();
  float peak = 0.0f;
  for (int block = 0; block < 800; ++block) {
    const bool rising = block == 0;
    const bool gate = block == 0;
    peak = max(peak, envelope.Process(0.0f, gate, rising));
  }
  if (peak < 0.999f || envelope.value() > 0.001f) {
    fprintf(
        stderr,
        "One-knob pluck did not complete: peak=%f tail=%f\n",
        peak,
        envelope.value());
    abort();
  }

  // The midpoint settles at half sustain while held, then follows the same
  // slow Elements release when the gate falls.
  envelope.Init();
  for (int block = 0; block < 16000; ++block) {
    envelope.Process(0.5f, true, block == 0);
  }
  if (fabsf(envelope.value() - 0.5f) > 0.001f) {
    fprintf(stderr, "One-knob midpoint sustain=%f\n", envelope.value());
    abort();
  }
  for (int block = 0; block < 16000; ++block) {
    envelope.Process(0.5f, false, false);
  }
  if (envelope.value() > 0.001f) {
    fprintf(stderr, "One-knob midpoint release tail=%f\n", envelope.value());
    abort();
  }

  // The right edge is a fast gate: full sustain while high, silence shortly
  // after release.
  envelope.Init();
  for (int block = 0; block < 500; ++block) {
    envelope.Process(1.0f, true, block == 0);
  }
  if (envelope.value() < 0.999f) {
    fprintf(stderr, "One-knob gated sustain=%f\n", envelope.value());
    abort();
  }

  // Live moves in the gated half update sustain immediately, and crossing
  // into the one-shot half must release rather than latch the old sustain.
  envelope.Process(0.5f, true, false);
  if (fabsf(envelope.value() - 0.5f) > 0.001f) {
    fprintf(stderr, "One-knob live sustain=%f\n", envelope.value());
    abort();
  }
  for (int block = 0; block < 16000; ++block) {
    envelope.Process(0.0f, true, false);
  }
  if (envelope.value() > 0.001f) {
    fprintf(stderr, "One-knob live region-crossing tail=%f\n", envelope.value());
    abort();
  }

  envelope.Init();
  for (int block = 0; block < 500; ++block) {
    envelope.Process(1.0f, true, block == 0);
  }
  for (int block = 0; block < 500; ++block) {
    envelope.Process(1.0f, false, false);
  }
  if (envelope.value() > 0.001f) {
    fprintf(stderr, "One-knob gated release tail=%f\n", envelope.value());
    abort();
  }
}

void TestExperimentalEngines() {
  printf("Validating LPC discrete frame bounds...\n");
  fflush(stdout);
  ValidateLPCDiscreteFrameBounds();
  printf("Validating Speech engine split anchors...\n");
  fflush(stdout);
  ValidateSpeechEngineSplit();
  printf("Validating the shared scale bank...\n");
  fflush(stdout);
  ValidateScaleVoiceBank();
  printf("Validating held-button pot routing...\n");
  fflush(stdout);
  ValidateDynamicHiddenPotRouting();
  printf("Validating automatic LEVEL routing and clocked Chiptune VCA...\n");
  fflush(stdout);
  ValidateClockedChiptuneLevelVca();
  ValidateAutoLevelDecayRouting();
  printf("Validating one-knob envelope...\n");
  fflush(stdout);
  ValidateOneKnobEnvelope();
  printf("Validating selectable chord tables...\n");
  fflush(stdout);
  BufferAllocator chord_allocator(ram_block, sizeof(ram_block));
  ChordBank chord_bank;
  chord_bank.Init(&chord_allocator);
  int previous_table_start = -1;
  for (int table = 0; table < PLAITS_CHORD_TABLE_COUNT; ++table) {
    chord_bank.set_chord(0.0f, table);
    const int table_start = chord_bank.chord_index();
    if (table_start <= previous_table_start) {
      fprintf(stderr, "Chord table %d has an invalid start index\n", table);
      abort();
    }
    previous_table_start = table_start;
    for (int position = 0; position <= 16; ++position) {
      chord_bank.set_chord(static_cast<float>(position) / 16.0f, table);
      if (chord_bank.num_notes() < 1 || chord_bank.num_notes() > kChordNumNotes) {
        fprintf(stderr, "Chord table %d has an invalid arpeggio length\n", table);
        abort();
      }
      for (int note = 0; note < kChordNumNotes; ++note) {
        if (!isfinite(chord_bank.ratio(note)) || chord_bank.ratio(note) <= 0.0f) {
          fprintf(stderr, "Chord table %d has an invalid pitch ratio\n", table);
          abort();
        }
      }
    }
  }
  chord_bank.set_chord(0.0f, 0);
  const int first_chord = chord_bank.chord_index();
  chord_bank.set_chord(0.0f, 0xff);
  if (chord_bank.chord_index() != first_chord) {
    fprintf(stderr, "Invalid chord table selection did not fall back to table 0\n");
    abort();
  }

  printf("Rendering Glisson sweep...\n");
  fflush(stdout);
  RenderExperimentalEngine<GlissonEngine>("plaits_glisson_engine.wav");
  printf("Rendering GENDY sweep...\n");
  fflush(stdout);
  RenderExperimentalEngine<GendyEngine>("plaits_gendy_engine.wav");
  printf("Rendering Scanned sweep...\n");
  fflush(stdout);
  RenderExperimentalEngine<ScannedEngine>("plaits_scanned_engine.wav");
  printf("Rendering Pulsar sweep...\n");
  fflush(stdout);
  RenderExperimentalEngine<PulsarEngine>("plaits_pulsar_engine.wav");
  printf("Rendering Plaits Lab audition files...\n");
  fflush(stdout);
  RenderAuditionEngine<LoopbackEngine>("01-loopback.wav");
  RenderAuditionEngine<LockstepEngine>("02-lockstep.wav");
  RenderAuditionEngine<TapfieldEngine>("03-tapfield.wav");
  RenderAuditionEngine<PhaseWeaveEngine>("04-phase-weave.wav");
  RenderAuditionEngine<SidebandEngine>("05-sideband-bank.wav");
  RenderAuditionEngine<AttractorEngine>("06-attractor.wav");
  RenderAuditionEngine<UndertowEngine>("07-undertow.wav");
  RenderAuditionEngine<ReedPipeEngine>("08-reed-pipe.wav");
  RenderAuditionEngine<PhaseFlockEngine>("09-phase-flock.wav");
  RenderAuditionEngine<RulefieldEngine>("10-rulefield.wav");
  RenderAuditionEngine<SpectralSpiralEngine>("11-spectral-spiral.wav");
  RenderAuditionEngine<ZFilterEngine>("12-z-filter.wav");
  RenderAuditionEngine<TripleEngine>("22-triple.wav");
  RenderAuditionEngine<BytebeatEngine>("23-bytebeat.wav");
  RenderAuditionEngine<DiatonicChordEngine>("24-diatonic-chord.wav");
  RenderAuditionEngine<ScaleStackEngine>("25-scale-stack.wav");
  RenderAuditionEngine<WavetableChordEngine>("52-wavetable-chord.wav");
  RenderAuditionEngine<WavetableScaleStackEngine>("53-wavetable-scale-stack.wav");
  RenderAuditionEngine<ShakersEngine>("26-shakers.wav");
  RenderAuditionEngine<BrassEngine>("28-brass.wav");
  RenderAuditionEngine<RawFmEngine>("21-raw-fm.wav");
  RenderAuditionEngine<VowelFofEngine>("20-vowel-fof.wav");
  RenderAuditionEngine<SawCombEngine>("19-saw-comb.wav");
  RenderAuditionEngine<DigitalModulationEngine>("18-digital-modulation.wav");
  RenderAuditionEngine<ToyEngine>("13-toy.wav");
  RenderAuditionEngine<CSawEngine>("14-csaw.wav");
  RenderAuditionEngine<RingModEngine>("15-ring-mod.wav");
  RenderAuditionEngine<BowedEngine>("16-bowed.wav");
  RenderAuditionEngine<QuestionMarkEngine>("51-question-mark.wav");
  RenderAuditionEngine<FlutedEngine>("50-fluted.wav");
  RenderAuditionEngine<WaveParaphonicEngine>("49-wave-paraphonic.wav");
  RenderAuditionEngine<WaveScanEngine>("48-wave-scan.wav");
  RenderAuditionEngine<CymbalEngine>("47-cymbal.wav");
  RenderAuditionEngine<SnareEngine>("46-snare.wav");
  RenderAuditionEngine<KickEngine>("45-kick.wav");
  RenderAuditionEngine<StruckDrumEngine>("44-struck-drum.wav");
  RenderAuditionEngine<StruckBellEngine>("43-struck-bell.wav");
  RenderAuditionEngine<BlownEngine>("42-blown.wav");
  RenderAuditionEngine<PluckedEngine>("41-plucked.wav");
  RenderAuditionEngine<VosimEngine>("40-vosim.wav");
  RenderAuditionEngine<HarmonicsEngine>("39-harmonics.wav");
  RenderAuditionEngine<VowelEngine>("38-vowel.wav");
  RenderAuditionEngine<SawSwarmEngine>("37-saw-swarm.wav");
  RenderAuditionEngine<SawSquareEngine>("36-saw-square.wav");
  RenderAuditionEngine<ParticleBurstEngine>("35-particle-burst.wav");
  RenderAuditionEngine<NoiseBankEngine>("34-noise-bank.wav");
  RenderAuditionEngine<MorphEngine>("33-morph.wav");
  RenderAuditionEngine<GranularCloudEngine>("32-granular-cloud.wav");
  RenderAuditionEngine<DualSyncEngine>("31-dual-sync.wav");
  RenderAuditionEngine<BuzzEngine>("30-buzz.wav");
  RenderAuditionEngine<SubOscillatorEngine>("17-sub-oscillator.wav");
  RenderAuditionEngine<FoldEngine>("29-fold.wav");
  ReportAuditionDcFailures();
  printf("Validating Glisson extremes...\n");
  fflush(stdout);
  ValidateExperimentalEngineExtremes<GlissonEngine>();
  printf("Validating GENDY extremes...\n");
  fflush(stdout);
  ValidateExperimentalEngineExtremes<GendyEngine>();
  printf("Validating Scanned extremes...\n");
  fflush(stdout);
  ValidateExperimentalEngineExtremes<ScannedEngine>();
  printf("Validating Pulsar extremes...\n");
  fflush(stdout);
  ValidateExperimentalEngineExtremes<PulsarEngine>();
  printf("Validating new Plaits Lab engines...\n");
  fflush(stdout);
  ValidateExperimentalEngineExtremes<LoopbackEngine>();
  ValidateExperimentalEngineExtremes<FormantSpeechEngine>(32.0f);
  ValidateExperimentalEngineExtremes<LPCSpeechEngine>(32.0f);
  ValidateExperimentalEngineExtremes<LockstepEngine>();
  ValidateExperimentalEngineExtremes<TapfieldEngine>();
  ValidateExperimentalEngineExtremes<PhaseWeaveEngine>();
  ValidateExperimentalEngineExtremes<SidebandEngine>();
  ValidateExperimentalEngineExtremes<AttractorEngine>();
  ValidateExperimentalEngineExtremes<UndertowEngine>();
  ValidateExperimentalEngineExtremes<ReedPipeEngine>();
  ValidateExperimentalEngineExtremes<PhaseFlockEngine>();
  ValidateExperimentalEngineExtremes<RulefieldEngine>();
  ValidateExperimentalEngineExtremes<SpectralSpiralEngine>();
  ValidateExperimentalEngineExtremes<DigitalModulationEngine>();
  ValidateExperimentalEngineExtremes<SawCombEngine>();
  ValidateExperimentalEngineExtremes<VowelFofEngine>();
  ValidateExperimentalEngineExtremes<RawFmEngine>();
  ValidateExperimentalEngineExtremes<TripleEngine>();
  ValidateExperimentalEngineExtremes<BytebeatEngine>();
  ValidateExperimentalEngineExtremes<DiatonicChordEngine>();
  ValidateExperimentalEngineExtremes<ScaleStackEngine>();
  ValidateExperimentalEngineExtremes<WavetableChordEngine>();
  ValidateExperimentalEngineExtremes<WavetableScaleStackEngine>();
  ValidateExperimentalEngineExtremes<ShakersEngine>();
  ValidateExperimentalEngineExtremes<BrassEngine>();
  ValidateExperimentalEngineExtremes<ZFilterEngine>();
  ValidateExperimentalEngineExtremes<ToyEngine>();
  ValidateExperimentalEngineExtremes<CSawEngine>();
  ValidateExperimentalEngineExtremes<RingModEngine>();
  ValidateExperimentalEngineExtremes<BowedEngine>();
  ValidateExperimentalEngineExtremes<QuestionMarkEngine>();
  ValidateExperimentalEngineExtremes<FlutedEngine>();
  ValidateExperimentalEngineExtremes<WaveParaphonicEngine>();
  ValidateExperimentalEngineExtremes<WaveScanEngine>();
  ValidateExperimentalEngineExtremes<CymbalEngine>();
  ValidateExperimentalEngineExtremes<SnareEngine>();
  ValidateExperimentalEngineExtremes<KickEngine>();
  ValidateExperimentalEngineExtremes<StruckDrumEngine>();
  ValidateExperimentalEngineExtremes<StruckBellEngine>();
  ValidateExperimentalEngineExtremes<BlownEngine>();
  ValidateExperimentalEngineExtremes<PluckedEngine>();
  ValidateExperimentalEngineExtremes<VosimEngine>();
  ValidateExperimentalEngineExtremes<HarmonicsEngine>();
  ValidateExperimentalEngineExtremes<VowelEngine>();
  ValidateExperimentalEngineExtremes<SawSwarmEngine>();
  ValidateExperimentalEngineExtremes<SawSquareEngine>();
  ValidateExperimentalEngineExtremes<ParticleBurstEngine>();
  ValidateExperimentalEngineExtremes<NoiseBankEngine>();
  ValidateExperimentalEngineExtremes<MorphEngine>();
  ValidateExperimentalEngineExtremes<GranularCloudEngine>();
  ValidateExperimentalEngineExtremes<DualSyncEngine>();
  ValidateExperimentalEngineExtremes<BuzzEngine>();
  ValidateExperimentalEngineExtremes<FoldEngine>();
  ValidateExperimentalEngineExtremes<SubOscillatorEngine>();
  ValidateExperimentalEngineExtremes<VirtualAnalogDualEngine>();
  ValidateExperimentalEngineExtremes<VirtualAnalogCrossfadeEngine>();
  ValidateExperimentalEngineExtremes<VirtualAnalogDualEngine>(4.0f, true);
  ValidateExperimentalEngineExtremes<VirtualAnalogCrossfadeEngine>(4.0f, true);
  ValidateExperimentalControlResponse<LoopbackEngine>("Loopback");
  ValidateExperimentalControlResponse<FormantSpeechEngine>("Formant Speech");
  ValidateExperimentalControlResponse<LPCSpeechEngine>("LPC Speech");
  ValidateExperimentalControlResponse<LockstepEngine>("Lockstep");
  ValidateExperimentalControlResponse<TapfieldEngine>("Tapfield");
  ValidateExperimentalControlResponse<PhaseWeaveEngine>("Phase Weave");
  ValidateExperimentalControlResponse<SidebandEngine>("Sideband Bank");
  ValidateExperimentalControlResponse<AttractorEngine>("Attractor");
  ValidateExperimentalControlResponse<UndertowEngine>("Undertow");
  ValidateExperimentalControlResponse<ReedPipeEngine>("Reed Pipe");
  ValidateExperimentalControlResponse<PhaseFlockEngine>("Phase Flock");
  ValidateExperimentalControlResponse<RulefieldEngine>("Rulefield");
  ValidateExperimentalControlResponse<SpectralSpiralEngine>("Spectral Spiral");
  ValidateExperimentalControlResponse<DigitalModulationEngine>("Digital Modulation");
  ValidateExperimentalControlResponse<SawCombEngine>("Saw Comb");
  ValidateExperimentalControlResponse<VowelFofEngine>("Vowel FOF");
  ValidateExperimentalControlResponse<RawFmEngine>("Raw FM");
  ValidateExperimentalControlResponse<TripleEngine>("Triple");
  ValidateExperimentalControlResponse<BytebeatEngine>("Bytebeat");
  ValidateExperimentalControlResponse<DiatonicChordEngine>("Diatonic Chord");
  ValidateExperimentalControlResponse<ScaleStackEngine>("Scale Stack");
  ValidateExperimentalControlResponse<WavetableChordEngine>(
      "Wavetable Diatonic Chord");
  ValidateExperimentalControlResponse<WavetableScaleStackEngine>("Wavetable Scale Stack");
  ValidateExperimentalControlResponse<ShakersEngine>("Shakers");
  ValidateExperimentalControlResponse<BrassEngine>("Brass");
  ValidateExperimentalControlResponse<ZFilterEngine>("Z Filter");
  ValidateExperimentalControlResponse<ToyEngine>("Toy");
  ValidateExperimentalControlResponse<CSawEngine>("CSaw");
  ValidateExperimentalControlResponse<RingModEngine>("Ring Mod");
  ValidateExperimentalControlResponse<BowedEngine>("Bowed");
  ValidateExperimentalControlResponse<QuestionMarkEngine>("Question Mark");
  ValidateExperimentalControlResponse<FlutedEngine>("Fluted");
  ValidateExperimentalControlResponse<WaveParaphonicEngine>("Wave Paraphonic");
  ValidateExperimentalControlResponse<WaveScanEngine>("Wave Scan");
  ValidateExperimentalControlResponse<CymbalEngine>("Cymbal");
  ValidateExperimentalControlResponse<SnareEngine>("Snare");
  ValidateExperimentalControlResponse<KickEngine>("Kick");
  ValidateExperimentalControlResponse<StruckDrumEngine>("Struck Drum");
  ValidateExperimentalControlResponse<StruckBellEngine>("Struck Bell");
  ValidateExperimentalControlResponse<BlownEngine>("Blown");
  ValidateExperimentalControlResponse<PluckedEngine>("Plucked");
  ValidateExperimentalControlResponse<VosimEngine>("VOSIM");
  ValidateExperimentalControlResponse<HarmonicsEngine>("Harmonics");
  ValidateExperimentalControlResponse<VowelEngine>("Vowel");
  ValidateExperimentalControlResponse<SawSwarmEngine>("Saw Swarm");
  ValidateExperimentalControlResponse<SawSquareEngine>("Saw Square");
  ValidateExperimentalControlResponse<ParticleBurstEngine>("Particle Burst");
  ValidateExperimentalControlResponse<NoiseBankEngine>("Noise Bank");
  ValidateExperimentalControlResponse<MorphEngine>("Morph");
  ValidateExperimentalControlResponse<GranularCloudEngine>("Granular Cloud");
  ValidateExperimentalControlResponse<DualSyncEngine>("Dual Sync");
  ValidateExperimentalControlResponse<BuzzEngine>("Buzz");
  ValidateExperimentalControlResponse<FoldEngine>("Fold");
  ValidateExperimentalControlResponse<SubOscillatorEngine>("Sub Osc");
  ValidateExperimentalControlResponse<VirtualAnalogDualEngine>("Virtual Analog Dual");
  ValidateExperimentalControlResponse<VirtualAnalogCrossfadeEngine>("Virtual Analog Crossfade");
  ValidateExperimentalControlResponse<VirtualAnalogDualEngine>(
      "Virtual Analog Dual stereo",
      true);
  ValidateExperimentalControlResponse<VirtualAnalogCrossfadeEngine>(
      "Virtual Analog Crossfade stereo",
      true);
  printf("Validating stock fourth-macro midpoint...\n");
  fflush(stdout);
  ValidateStockMacroMidpoint();
  printf("Validating stock fourth-macro responses...\n");
  fflush(stdout);
  printf("  VA + VCF\n");
  fflush(stdout);
  ValidateStockMacroResponse<VirtualAnalogVCFEngine>("VA + VCF");
  printf("  String Machine\n");
  fflush(stdout);
  ValidateStockMacroResponse<StringMachineEngine>("String Machine");
  printf("  Chords\n");
  fflush(stdout);
  ValidateStockMacroResponse<ChordEngine>("Chords");
  printf("  Filtered Noise\n");
  fflush(stdout);
  ValidateStockMacroResponse<NoiseEngine>("Filtered Noise");
  printf("  Particle Noise\n");
  fflush(stdout);
  ValidateStockMacroResponse<ParticleEngine>("Particle Noise");
  printf("  Analog Bass Drum\n");
  fflush(stdout);
  ValidateStockMacroResponse<BassDrumEngine>("Analog Bass Drum");
  printf("  Phase Distortion\n");
  ValidateStockMacroResponse<PhaseDistortionEngine>("Phase Distortion");
  printf("  Six-op FM\n");
  ValidateSixOpMacroResponse();
  printf("  Six-op FM short bank\n");
  ValidateSixOpShortBank();
  printf("  Six-op FM bank switch\n");
  ValidateSixOpBankSwitch();
  printf("  Wave Terrain\n");
  ValidateStockMacroResponse<WaveTerrainEngine>("Wave Terrain");
  printf("  Chiptune\n");
  ValidateStockMacroResponse<ChiptuneEngine>("Chiptune");
  printf("  Virtual Analog\n");
  ValidateStockMacroResponse<VirtualAnalogEngine>("Virtual Analog");
  printf("  Virtual Analog Dual\n");
  ValidateStockMacroResponse<VirtualAnalogDualEngine>("Virtual Analog Dual");
  printf("  Virtual Analog Crossfade\n");
  ValidateStockMacroResponse<VirtualAnalogCrossfadeEngine>("Virtual Analog Crossfade");
  printf("  Waveshaping\n");
  ValidateStockMacroResponse<WaveshapingEngine>("Waveshaping");
  printf("  Two-op FM\n");
  ValidateStockMacroResponse<FMEngine>("Two-op FM");
  printf("  Granular Formant\n");
  ValidateStockMacroResponse<GrainEngine>("Granular Formant");
  printf("  Harmonic Oscillator\n");
  ValidateStockMacroResponse<AdditiveEngine>("Harmonic Oscillator");
  printf("  Wavetable\n");
  ValidateStockMacroResponse<WavetableEngine>("Wavetable");
  printf("  Speech\n");
  ValidateStockMacroResponse<SpeechEngine>("Speech");
  printf("  Swarm\n");
  ValidateStockMacroResponse<SwarmEngine>("Swarm");
  printf("  Inharmonic String\n");
  ValidateStockMacroResponse<StringEngine>("Inharmonic String");
  printf("  Modal Resonator\n");
  ValidateStockMacroResponse<ModalEngine>("Modal Resonator");
  printf("  Analog Snare\n");
  ValidateStockMacroResponse<SnareDrumEngine>("Analog Snare");
  printf("  Analog Hi-hat\n");
  ValidateStockMacroResponse<HiHatEngine>("Analog Hi-hat");
  fflush(stdout);
  printf("Validating stock fourth-macro extremes...\n");
  fflush(stdout);
  // Stock engines are normally followed by their registered gain/limiter.
  // Use a wider raw-output ceiling here while still catching instability.
  // Voice applies each engine's registered gain and limiter after this stage.
  ValidateExperimentalEngineExtremes<VirtualAnalogVCFEngine>(32.0f);
  ValidateExperimentalEngineExtremes<StringMachineEngine>(32.0f);
  ValidateExperimentalEngineExtremes<ChordEngine>(32.0f);
  ValidateExperimentalEngineExtremes<NoiseEngine>(32.0f);
  ValidateExperimentalEngineExtremes<ParticleEngine>(32.0f);
  ValidateExperimentalEngineExtremes<BassDrumEngine>(32.0f);
  ValidateExperimentalEngineExtremes<PhaseDistortionEngine>(32.0f);
  ValidateExperimentalEngineExtremes<WaveTerrainEngine>(32.0f);
  ValidateExperimentalEngineExtremes<ChiptuneEngine>(32.0f);
  ValidateExperimentalEngineExtremes<VirtualAnalogEngine>(32.0f);
  ValidateExperimentalEngineExtremes<VirtualAnalogDualEngine>(32.0f);
  ValidateExperimentalEngineExtremes<VirtualAnalogCrossfadeEngine>(32.0f);
  ValidateExperimentalEngineExtremes<WaveshapingEngine>(32.0f);
  ValidateExperimentalEngineExtremes<FMEngine>(32.0f);
  ValidateExperimentalEngineExtremes<GrainEngine>(32.0f);
  ValidateExperimentalEngineExtremes<AdditiveEngine>(32.0f);
  ValidateExperimentalEngineExtremes<WavetableEngine>(32.0f);
  ValidateExperimentalEngineExtremes<SpeechEngine>(32.0f);
  ValidateExperimentalEngineExtremes<SwarmEngine>(32.0f);
  ValidateExperimentalEngineExtremes<StringEngine>(32.0f);
  ValidateExperimentalEngineExtremes<ModalEngine>(32.0f);
  ValidateExperimentalEngineExtremes<SnareDrumEngine>(32.0f);
  ValidateExperimentalEngineExtremes<HiHatEngine>(32.0f);
  printf("Synthesis engine tests passed.\n");
}

void ValidateParameterRandomizer() {
  const ParameterRandomizerProfile profile = {
    0.25f, 0.55f, 0.000009f, 0.000006f, 0.60f
  };
  ParameterRandomizer randomizer;
  randomizer.Init();

  float timbre = 0.4f;
  float morph = 0.6f;
  randomizer.Process(
      ATTENUVERTER_MODE_DRIFT, true, true,
      0.4f, 0.6f, 0.05f, -0.05f, profile, profile,
      &timbre, &morph);
  if (timbre != 0.4f || morph != 0.6f) {
    fprintf(stderr, "Attenuverter dead zone is not exactly off\n");
    abort();
  }

  float first_drift = 0.0f;
  for (int i = 0; i < 20000; ++i) {
    randomizer.Process(
        ATTENUVERTER_MODE_DRIFT, true, false,
        0.5f, 0.5f, 0.8f, 0.8f, profile, profile,
        &timbre, &morph);
    if (i == 0) {
      first_drift = timbre;
    }
    if (timbre < 0.0f || timbre > 1.0f || morph != 0.6f) {
      fprintf(stderr, "Drift escaped its bounds or touched a disabled axis\n");
      abort();
    }
  }
  if (fabsf(timbre - first_drift) < 0.001f) {
    fprintf(stderr, "Drift did not move over time\n");
    abort();
  }

  randomizer.Process(
      ATTENUVERTER_MODE_STEP, true, true,
      0.5f, 0.5f, 0.8f, -0.8f, profile, profile,
      &timbre, &morph);
  const float held_timbre = timbre;
  const float held_morph = morph;
  for (int i = 0; i < 100; ++i) {
    randomizer.Process(
        ATTENUVERTER_MODE_STEP, true, true,
        0.5f, 0.5f, 0.8f, -0.8f, profile, profile,
        &timbre, &morph);
  }
  if (timbre != held_timbre || morph != held_morph) {
    fprintf(stderr, "Step mode moved without a trigger\n");
    abort();
  }
  randomizer.Trigger();
  randomizer.Process(
      ATTENUVERTER_MODE_STEP, true, true,
      0.5f, 0.5f, 0.8f, -0.8f, profile, profile,
      &timbre, &morph);
  if (timbre == held_timbre && morph == held_morph) {
    fprintf(stderr, "Step mode did not choose a new trigger value\n");
    abort();
  }
}

void ValidateCustomSpeechBankLevelMatching() {
  if (MatchCustomSpeechBankEnergy(0) != 0 ||
      MatchCustomSpeechBankEnergy(1) != 5 ||
      MatchCustomSpeechBankEnergy(32) != 160 ||
      MatchCustomSpeechBankEnergy(33) != 161 ||
      MatchCustomSpeechBankEnergy(255) != 161) {
    fprintf(stderr, "Custom Speech bank energy matching is incorrect\n");
    abort();
  }
}

int main(void) {
#if defined(__SSE2__)
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif
  // TestFormantOscillator();
  // TestGrainletOscillator();
  // TestOscillator();
  // TestVariableShapeOscillator();
  // TestStringSynthOscillator();
  // TestStringSynthOscillator();
  // TestVosimOscillator();
  // TestZOscillator();
  // TestHarmonicOscillator();
  // TestWavetableOscillator();
  // TestNESTriangleOscillator();
  // TestSuperSquareOscillator();
  // TestVariableSawOscillator();
  
  // TestAdditiveEngine();
  // TestChiptuneEngine();
  // TestChordEngine();
  // TestFMEngine();
  // TestGrainEngine();
  // TestModalEngine();
  // TestStringEngine();
  // TestNoiseEngine();
  // TestParticleEngine();
  // TestPhaseDistortionEngine();
  // TestSpeechEngine();
  // TestStringMachineEngine();
  // TestSwarmEngine();
  // TestVirtualAnalogEngine();
  // TestVirtualAnalogVCFEngine();
  // TestWaveshapingEngine();
  // TestWavetableEngine();
  // TestWaveTerrainEngine();

  // TestBassDrumEngine();
  // TestSnareDrumEngine();
  // TestHiHatEngine();
  
  // TestSampleRateReducer();
  // TestVoice();
  // TestFMGlitch();
  // TestLimiterGlitch();
  // EnumerateWavetables();
  
  // TestLPGAttackDecay();
  printf("Validating unpatched attenuverter modes...\n");
  ValidateParameterRandomizer();
  printf("Validating custom Speech bank level matching...\n");
  ValidateCustomSpeechBankLevelMatching();
  TestExperimentalEngines();
}
