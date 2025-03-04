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
#include "plaits/dsp/voice.h"
#include "plaits/settings.h"
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
UserData user_data;
UserDataReceiver user_data_receiver;
Voice voice;

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

  IWDG_ReloadCounter();
  
  ui.Poll();
  
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
  } else {
    if (modulations.timbre_patched) {
      PacketDecoderState state = \
          user_data_receiver.Process(modulations.timbre);
      if (state == PACKET_DECODER_STATE_END_OF_TRANSMISSION) {
        if (user_data_receiver.progress() == 1.0f) {
          int slot = voice.active_engine();
          bool success = user_data.Save(user_data_receiver.rx_buffer(), slot);
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
    voice.Render(patch, modulations, (Voice::Frame*)(output), size);
    ui.set_active_engine(voice.active_engine());
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
  
  audio_dac.Init(48000, kBlockSize);

  audio_dac.Start(&FillBuffer);
  IWDG_Enable();
}

int main(void) {
  Init();
  while (1) { }
}
