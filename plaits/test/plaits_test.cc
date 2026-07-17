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
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"

#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/gendy_engine.h"
#include "plaits/dsp/engine2/glisson_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/scanned_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
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

template<typename T>
void ValidateExperimentalEngineExtremes(float maximum = 4.0f) {
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
      const double patch_threshold = max(1.0, patch_stock * 0.001);
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

void TestExperimentalEngines() {
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
  printf("  Wave Terrain\n");
  ValidateStockMacroResponse<WaveTerrainEngine>("Wave Terrain");
  printf("  Chiptune\n");
  ValidateStockMacroResponse<ChiptuneEngine>("Chiptune");
  printf("  Virtual Analog\n");
  ValidateStockMacroResponse<VirtualAnalogEngine>("Virtual Analog");
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
  TestExperimentalEngines();
}
