// Copyright 2026 Rubato Audio.

#include "plaits/drivers/audio_rate_timer.h"

#include "plaits/build_config.h"

#if PLAITS_BUILD_ENABLE_SYNC_INPUT

#include <stm32f37x_conf.h>

namespace plaits {

bool AudioRateTimer::initialized_;
uint8_t AudioRateTimer::active_clients_;

void AudioRateTimer::Init() {
  if (initialized_) {
    return;
  }

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

  TIM_TimeBaseInitTypeDef timer;
  TIM_TimeBaseStructInit(&timer);
  timer.TIM_Prescaler = 0;
  // The I2S sample clock is 72 MHz / (47 * 32) = 47,872.34 Hz.
  timer.TIM_Period = 1504 - 1;
  timer.TIM_CounterMode = TIM_CounterMode_Up;
  timer.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseInit(TIM2, &timer);

  // ADC1 uses the update event (TRGO) for the injected MODEL conversion.
  TIM_SelectOutputTrigger(TIM2, TIM_TRGOSource_Update);

  TIM_SetCounter(TIM2, 0);
  initialized_ = true;
}

void AudioRateTimer::SetClientActive(Client client, bool active) {
  Init();

  uint8_t previous = active_clients_;
  if (active) {
    active_clients_ |= static_cast<uint8_t>(client);
  } else {
    active_clients_ &= ~static_cast<uint8_t>(client);
  }

  if (!previous && active_clients_) {
    TIM_SetCounter(TIM2, 0);
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    TIM_Cmd(TIM2, ENABLE);
  } else if (previous && !active_clients_) {
    TIM_Cmd(TIM2, DISABLE);
  }
}

}  // namespace plaits

#endif  // PLAITS_BUILD_ENABLE_SYNC_INPUT
