// Copyright 2026 Rubato Audio.
//
// Shared audio-rate trigger timer for Plaits Palette extensions.

#ifndef PLAITS_DRIVERS_AUDIO_RATE_TIMER_H_
#define PLAITS_DRIVERS_AUDIO_RATE_TIMER_H_

#include <stdint.h>

namespace plaits {

// TIM2 is shared by the MODEL-input hard-sync detector and the optional
// audio-rate TZFM CV path. Keep its configuration in one place so enabling one
// feature cannot reset or stop the other feature's sampling clock.
class AudioRateTimer {
 public:
  enum Client {
    CLIENT_SYNC_INPUT = 1 << 0,
    CLIENT_TZFM_CV = 1 << 1
  };

  // Safe to call more than once. This configures both trigger outputs:
  //   TIM2 TRGO/update -> ADC1 injected MODEL-input conversions.
  //   TIM2 CC3         -> SDADC2 injected TZFM conversions.
  static void Init();

  // Starts TIM2 while at least one client is active. A client can acquire or
  // release the timer repeatedly without disturbing other clients.
  static void SetClientActive(Client client, bool active);

  static inline bool running() { return active_clients_ != 0; }

 private:
  static bool initialized_;
  static uint8_t active_clients_;
};

}  // namespace plaits

#endif  // PLAITS_DRIVERS_AUDIO_RATE_TIMER_H_
