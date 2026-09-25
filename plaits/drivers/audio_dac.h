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
//
// -----------------------------------------------------------------------------
//
// Drivers for the PCM5100 codec.

#ifndef PLAITS_DRIVERS_DAC_H_
#define PLAITS_DRIVERS_DAC_H_

#include "stmlib/stmlib.h"

// Late-fill instrumentation for the overrun diagnostics (overrun_sweep.h,
// threshold_ladder.h); compiled out of ordinary firmware.
#if PLAITS_OVERRUN_SWEEP || PLAITS_THRESHOLD_LADDER
#define PLAITS_DAC_LATE_PROBE 1
#else
#define PLAITS_DAC_LATE_PROBE 0
#endif

namespace plaits {

const size_t kMaxCodecBlockSize = 24;

class AudioDac {
 public:
  AudioDac() { }
  ~AudioDac() { }
  
  typedef struct {
    short l;
    short r;
  } Frame;
  
  typedef void (*FillBufferCallback)(Frame* tx, size_t size);
  
  void Init(int sample_rate, size_t block_size);
  void Start(FillBufferCallback callback);
  void Stop();
  void Fill(size_t offset);
  
  static AudioDac* GetInstance() { return instance_; }

#if PLAITS_DAC_LATE_PROBE
  // Overrun-sweep instrumentation (plaits/overrun_sweep.h). A late fill is a
  // refill that finished after the DMA had already entered that half, so the
  // DAC played at least one stale frame. A double-pending entry means both
  // halves were due at once: the render fell a whole half-block behind.
  inline uint32_t late_fills() const { return late_fills_; }
  inline uint32_t double_pending() const { return double_pending_; }
  inline void CountDoublePending() { ++double_pending_; entry_double_ = true; }
  // Whether the DMA has already entered the half being filled -- asked right
  // after Render, where production firmware has written its last sample.
  bool OutputLate() const;
  // Frames the DMA had already played of the other half: how long after the
  // DMA event the callback got going.
  uint32_t EntryLag() const;
#endif

 private:
  void InitializeGPIO();
  void InitializeAudioInterface(int sample_rate);
  void InitializeDMA(size_t block_size);
  static AudioDac* instance_;
  
  size_t block_size_;
  FillBufferCallback callback_;
  
  Frame tx_dma_buffer_[kMaxCodecBlockSize * 2];
#if PLAITS_DAC_LATE_PROBE
  volatile uint32_t late_fills_;
  volatile uint32_t double_pending_;
  volatile size_t filling_;
  volatile bool entry_double_;
#endif
  
  DISALLOW_COPY_AND_ASSIGN(AudioDac);
};

}  // namespace plaits

#endif  // PLAITS_DRIVERS_DAC_H_
