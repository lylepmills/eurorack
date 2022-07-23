// Copyright 2013 Emilie Gillet.
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
//
// -----------------------------------------------------------------------------
//
// Midi event handler.

#ifndef RINGS_MIDI_HANDLER_H_
#define RINGS_MIDI_HANDLER_H_

#include "stmlib/stmlib.h"

#include "stmlib/utils/ring_buffer.h"
#include "stmlib/midi/midi.h"

#include "rings/dsp/part.h"
#include "rings/ui.h"

namespace rings {

class MidiHandler {
 public:
  // NOTE: original (from yarns) is 128 but anything over 32 currently makes RAM overflow
  typedef stmlib::RingBuffer<uint8_t, 32> MidiBuffer;
   
  MidiHandler() { }
  ~MidiHandler() { }
  
  static void Init(Ui* ui, Part* part);
  
  static void NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    part_->MidiNoteOn(note, velocity);
    ui_->BlinkLights();
  }
  
  static void NoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
  }
  
  static void Aftertouch(uint8_t channel, uint8_t note, uint8_t velocity) {
  }
  
  static void Aftertouch(uint8_t channel, uint8_t velocity) {
  }
  
  static void ControlChange(
      uint8_t channel,
      uint8_t controller,
      uint8_t value) {
  }
  
  static void ProgramChange(uint8_t channel, uint8_t program) {
  }
  
  static void PitchBend(uint8_t channel, uint16_t pitch_bend) {
  }

  static void SysExStart() { }

  static void SysExByte(uint8_t sysex_byte) { }
  
  static void SysExEnd() { }
  
  static void BozoByte(uint8_t bozo_byte) { }

  static void Clock() { }
  
  static void Start() { }
  
  static void Continue() { }
  
  static void Stop() { }
  
  static void Reset() { }
  
  static bool CheckChannel(uint8_t channel) { return true; }

  static void RawByte(uint8_t byte) { }
  
  static void RawMidiData(
      uint8_t status,
      uint8_t* data,
      uint8_t data_size,
      uint8_t accepted_channel) {
  }
  
  static void OnInternalNoteOn(
      uint8_t channel,
      uint8_t note,
      uint8_t velocity) {
  }
  
  static void OnInternalNoteOff(uint8_t channel, uint8_t note) { }
  
  static void OnClock() { }
  
  static void OnStart() { }
  
  static void OnStop() { }
  
  static void PushByte(uint8_t byte) {
    input_buffer_.Overwrite(byte);
  }
  
  static void ProcessInput() {
    while (input_buffer_.readable()) {
      parser_.PushByte(input_buffer_.ImmediateRead());
    }
  }

  static void Flush() { }
  
 private:  
  static MidiBuffer input_buffer_; 
  static stmlib_midi::MidiStreamParser<MidiHandler> parser_;

  static Part* part_;
  static Ui* ui_;
     
  DISALLOW_COPY_AND_ASSIGN(MidiHandler);
};

extern MidiHandler midi_handler;

}  // namespace rings

#endif // RINGS_MIDI_HANDLER_H_
