// Copyright 2016 Emilie Gillet.
// 
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.

#include <stm32f37x_conf.h>

#include "plaits/drivers/audio_dac.h"

#include "plaits/dsp/dsp.h"
#include "plaits/cpu_probe.h"

#ifndef PLAITS_CPU_PROBE_SECTION_TOTAL
#define PLAITS_CPU_PROBE_SECTION_TOTAL 0
#endif

// The standard engine-development probe begins immediately before
// Voice::Render. System-feature benchmarks can opt into measuring the UI/ADC
// work that precedes it as part of the same audio callback.
#ifndef PLAITS_CPU_PROBE_WHOLE_CALLBACK
#define PLAITS_CPU_PROBE_WHOLE_CALLBACK 0
#endif
#include "plaits/dsp/voice.h"
#include "plaits/settings.h"
#if PLAITS_TZFM_DIAGNOSTIC
#include "plaits/tzfm_diagnostic.h"
#endif
#include "plaits/ui.h"
#include "plaits/user_data.h"
#include "plaits/user_data_receiver.h"

using namespace plaits;
using namespace stm_audio_bootloader;
using namespace stmlib;

// #define PROFILE_INTERRUPT 1

const bool test_adc_noise = false;

AudioDac audio_dac;
Modulations modulations;
Patch patch;
Settings settings;
Ui ui;
PLAITS_CPU_PROBE_DECLARE
UserData user_data;
UserDataReceiver user_data_receiver;
Voice voice;
#if PLAITS_TZFM_DIAGNOSTIC
TzfmDiagnostic tzfm_diagnostic;
#endif

char shared_buffer[16384];
uint32_t test_ramp;

// Default interrupt handlers.
extern "C" {

void NMI_Handler() { }
void HardFault_Handler() { while (1); }
void MemManage_Handler() { while (1); }
void BusFault_Handler() { while (1); }
void UsageFault_Handler() { while (1); }
void SVC_Handler() { }
void DebugMon_Handler() { }
void PendSV_Handler() { }
void __cxa_pure_virtual() { while (1); }

}

void FillBuffer(AudioDac::Frame* output, size_t size) {
#ifdef PROFILE_INTERRUPT
  TIC
#endif  // PROFILE_INTERRUPT

#if PLAITS_CPU_PROBE && PLAITS_CPU_PROBE_WHOLE_CALLBACK
  PLAITS_CPU_PROBE_BEGIN
#endif

  IWDG_ReloadCounter();

#if PLAITS_TZFM_DIAGNOSTIC
  // The Fast FM resampler and calibration live in Ui::Poll, while the regular
  // engine probe brackets Voice::Render. Measure both real callback portions
  // separately and add them; keep the synthetic sweep setup below outside the
  // ledger so the diagnostic does not charge its own control machinery to the
  // production feature.
  cpu_probe.Begin();
#endif
  ui.Poll();
#if PLAITS_TZFM_DIAGNOSTIC
  cpu_probe.End(size);
  const float diagnostic_ui_usage = cpu_probe.last_usage();
#endif
  
  if (test_adc_noise) {
    static float note_lp = 0.0f;
    float note = modulations.note;
    ONE_POLE(note_lp, note, 0.0001f);
    float cents = (note - note_lp) * 100.0f;
    CONSTRAIN(cents, -8.0f, +8.0f);
    while (size--) {
      output->r = output->l = static_cast<int16_t>(cents * 4040.0f);
      ++output;
    }
  } else if (ui.test_mode()) {
    // 100 Hz ascending and descending ramps.
    while (size--) {
      output->l = ~test_ramp >> 16;
      output->r = test_ramp >> 16;
      test_ramp += 8947848;
      ++output;
    }
  }
#if PLAITS_TZFM_DIAGNOSTIC
  else if (tzfm_diagnostic.reporting()) {
    ui.SetAudioRateFmNeeded(false);
    tzfm_diagnostic.WriteReport((Voice::Frame*)(output), size);
  }
#endif
  else {
#if PLAITS_TZFM_DIAGNOSTIC
    tzfm_diagnostic.Prepare(&patch, &modulations, size);
#endif
    if (modulations.timbre_patched) {
      PacketDecoderState state = \
          user_data_receiver.Process(modulations.timbre);
      if (state == PACKET_DECODER_STATE_END_OF_TRANSMISSION) {
        if (user_data_receiver.progress() == 1.0f) {
          int slot = voice.active_engine();
          bool success = user_data.Save(
              user_data_receiver.rx_buffer(), slot, patch.harmonics);
          if (success) {
            voice.ReloadUserData();
          } else {
            ui.DisplayDataTransferProgress(-1.0f);
          }
        }
        user_data_receiver.Reset();
      } else if (state == PACKET_DECODER_STATE_OK) {
        ui.DisplayDataTransferProgress(user_data_receiver.progress());
      } else if (state == PACKET_DECODER_STATE_ERROR_CRC) {
        ui.DisplayDataTransferProgress(-1.0f);
      }
    }
#if !(PLAITS_CPU_PROBE && PLAITS_CPU_PROBE_WHOLE_CALLBACK)
    PLAITS_CPU_PROBE_BEGIN
#endif
#if PLAITS_CPU_PROBE && PLAITS_CPU_PROBE_SECTION_TOTAL
    // Validation builds only: registering a section switches the AUX readout
    // from the continuous usage tone to the full two-tone beacon format.
    cpu_probe.SectionBegin(0);
#endif
    const int previous_engine = voice.active_engine();
    voice.Render(patch, modulations, (Voice::Frame*)(output), size);
    const int active_engine = voice.active_engine();
#if PLAITS_CPU_PROBE && PLAITS_CPU_PROBE_SECTION_TOTAL
    cpu_probe.SectionEnd(0);
#endif
    PLAITS_CPU_PROBE_END(size)
#if PLAITS_TZFM_DIAGNOSTIC
    TzfmDiagnosticCounters counters;
    counters.overruns = ui.audio_rate_fm_overruns();
    counters.resyncs = ui.audio_rate_fm_resyncs();
    counters.underflows = ui.audio_rate_fm_underflows();
    counters.excess_lag = ui.audio_rate_fm_excess_lag();
    tzfm_diagnostic.Observe(
        diagnostic_ui_usage + cpu_probe.last_usage(), counters);
    tzfm_diagnostic.Mute((Voice::Frame*)(output), size);
#endif
    PLAITS_CPU_PROBE_READOUT((Voice::Frame*)(output), size)
    PLAITS_CPU_PROBE_DISPLAY(ui)
    if (active_engine != previous_engine) {
      ui.RealignAudioInputAfterEngineChange();
    }
    ui.set_active_engine(active_engine);
#if PLAITS_BUILD_FAST_FM
#if PLAITS_TZFM_DIAGNOSTIC
    ui.SetAudioRateFmNeeded(tzfm_diagnostic.fast_stage());
#else
    ui.SetAudioRateFmNeeded(
        modulations.frequency_patched &&
        voice.active_engine_supports_fast_fm());
#endif
#endif
  }
  
#ifdef PROFILE_INTERRUPT
  TOC
#endif  // PROFILE_INTERRUPT
}

void Init() {
  NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x8000);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_16);
  
  BufferAllocator allocator(shared_buffer, 16384);
  voice.Init(&allocator);
  user_data_receiver.Init(
      (uint8_t*)(&shared_buffer[16384 - UserData::SIZE]),
      UserData::SIZE);
  
  volatile size_t counter = 1000000;
  while (counter--);

  settings.Init();
  ui.Init(&patch, &modulations, &settings);
  
  PLAITS_CPU_PROBE_INIT
#if PLAITS_TZFM_DIAGNOSTIC
  tzfm_diagnostic.Init(
      PLAITS_TZFM_AUDITION_GROUP,
      PLAITS_FM_DIAGNOSTIC_EXPONENTIAL);
#endif
  audio_dac.Init(48000, kBlockSize);

  audio_dac.Start(&FillBuffer);
  IWDG_Enable();
}

int main(void) {
  Init();
  while (1) { }
}
