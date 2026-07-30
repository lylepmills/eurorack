// Copyright 2026 Rubato Audio.
//
// Audio-rate edge detector for the conditioned MODEL CV input.

#ifndef PLAITS_DRIVERS_SYNC_INPUT_H_
#define PLAITS_DRIVERS_SYNC_INPUT_H_

#include <stddef.h>
#include <stdint.h>

#include "plaits/sync_event_queue.h"
#include "stmlib/stmlib.h"

namespace plaits {

class SyncInput {
 public:
  SyncInput() { }
  ~SyncInput() { }

  // Call after PotsAdc::Init(), since this augments ADC1 with an injected
  // channel while PotsAdc owns its regular scan and calibration.
  void Init();
  void SetEnabled(bool enabled);

  uint32_t ReadEvents(size_t block_size);
  inline bool enabled() const { return enabled_; }
  inline uint32_t dropped_events() const { return queue_.dropped(); }

  void HandleInterrupt();
  static inline SyncInput* GetInstance() { return instance_; }

 private:
  static uint32_t CycleCount();
  void ArmRisingEdge();
  void ArmRelease();

  static SyncInput* instance_;

  SyncEventQueue queue_;
  volatile bool armed_for_rising_edge_;
  bool enabled_;

  DISALLOW_COPY_AND_ASSIGN(SyncInput);
};

}  // namespace plaits

#endif  // PLAITS_DRIVERS_SYNC_INPUT_H_
