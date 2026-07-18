// Copyright 2026 Rubato Audio.
//
// Static registry used by the legacy PLAITS_STOCK_ENGINE_LAYOUT build flag.
// Recipe-driven builds force-include their generated engine_config.h instead.

#ifndef PLAITS_DSP_STOCK_ENGINE_CONFIG_H_
#define PLAITS_DSP_STOCK_ENGINE_CONFIG_H_

#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/engine/bass_drum_engine.h"
#include "plaits/dsp/engine/chord_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/hi_hat_engine.h"
#include "plaits/dsp/engine/modal_engine.h"
#include "plaits/dsp/engine/noise_engine.h"
#include "plaits/dsp/engine/particle_engine.h"
#include "plaits/dsp/engine/snare_drum_engine.h"
#include "plaits/dsp/engine/speech_engine.h"
#include "plaits/dsp/engine/string_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/wavetable_engine.h"
#include "plaits/dsp/engine2/chiptune_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine2/string_machine_engine.h"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"

#define PLAITS_HAS_SPEECH_ENGINE 1
#define PLAITS_HAS_CHIPTUNE_ENGINE 1
#define PLAITS_HAS_USER_DATA_BANK 1

#define PLAITS_ENGINE_MEMBERS \
  VirtualAnalogVCFEngine virtual_analog_vcf_engine_; \
  PhaseDistortionEngine phase_distortion_engine_; \
  SixOpEngine six_op_engine_; \
  WaveTerrainEngine wave_terrain_engine_; \
  StringMachineEngine string_machine_engine_; \
  ChiptuneEngine chiptune_engine_; \
  VirtualAnalogEngine virtual_analog_engine_; \
  WaveshapingEngine waveshaping_engine_; \
  FMEngine fm_engine_; \
  GrainEngine grain_engine_; \
  AdditiveEngine additive_engine_; \
  WavetableEngine wavetable_engine_; \
  ChordEngine chord_engine_; \
  SpeechEngine speech_engine_; \
  SwarmEngine swarm_engine_; \
  NoiseEngine noise_engine_; \
  ParticleEngine particle_engine_; \
  StringEngine string_engine_; \
  ModalEngine modal_engine_; \
  BassDrumEngine bass_drum_engine_; \
  SnareDrumEngine snare_drum_engine_; \
  HiHatEngine hi_hat_engine_;

#define PLAITS_REGISTER_ENGINES(registry) do { \
  (registry).RegisterInstance(&virtual_analog_vcf_engine_, false, 1.0f, 1.0f); \
  (registry).RegisterInstance(&phase_distortion_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&six_op_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&six_op_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&six_op_engine_, true, 1.0f, 1.0f); \
  (registry).RegisterInstance(&wave_terrain_engine_, false, 0.7f, 0.7f); \
  (registry).RegisterInstance(&string_machine_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&chiptune_engine_, false, 0.5f, 0.5f); \
  (registry).RegisterInstance(&virtual_analog_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&waveshaping_engine_, false, 0.7f, 0.6f); \
  (registry).RegisterInstance(&fm_engine_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&grain_engine_, false, 0.7f, 0.6f); \
  (registry).RegisterInstance(&additive_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&wavetable_engine_, false, 0.6f, 0.6f); \
  (registry).RegisterInstance(&chord_engine_, false, 0.8f, 0.8f); \
  (registry).RegisterInstance(&speech_engine_, false, -0.7f, 0.8f); \
  (registry).RegisterInstance(&swarm_engine_, false, -3.0f, 1.0f); \
  (registry).RegisterInstance(&noise_engine_, false, -1.0f, -1.0f); \
  (registry).RegisterInstance(&particle_engine_, false, -2.0f, 1.0f); \
  (registry).RegisterInstance(&string_engine_, true, -1.0f, 0.8f); \
  (registry).RegisterInstance(&modal_engine_, true, -1.0f, 0.8f); \
  (registry).RegisterInstance(&bass_drum_engine_, true, 0.8f, 0.8f); \
  (registry).RegisterInstance(&snare_drum_engine_, true, 0.8f, 0.8f); \
  (registry).RegisterInstance(&hi_hat_engine_, true, 0.8f, 0.8f); \
} while (0)

namespace plaits {

static const int8_t kEngineUserDataBank[24] = {
  -1, -1, 0, 1, 2, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1,
};
static const uint32_t kSpeechEngineMask = 0x00008000;
static const uint32_t kChiptuneEngineMask = 0x00000080;

}  // namespace plaits

#endif  // PLAITS_DSP_STOCK_ENGINE_CONFIG_H_
