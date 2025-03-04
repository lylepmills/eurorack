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
// Main synthesis voice.

#include "plaits/dsp/voice.h"
#include "plaits/user_data.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void Voice::Init(BufferAllocator* allocator) {
  engines_.Init();

  engines_.RegisterInstance(&virtual_analog_vcf_engine_, false, 1.0f, 1.0f);
  engines_.RegisterInstance(&phase_distortion_engine_, false, 0.7f, 0.7f);
  engines_.RegisterInstance(&six_op_engine_, true, 1.0f, 1.0f);
  engines_.RegisterInstance(&six_op_engine_, true, 1.0f, 1.0f);
  engines_.RegisterInstance(&six_op_engine_, true, 1.0f, 1.0f);
  engines_.RegisterInstance(&wave_terrain_engine_, false, 0.7f, 0.7f);
  engines_.RegisterInstance(&string_machine_engine_, false, 0.8f, 0.8f);
  engines_.RegisterInstance(&chiptune_engine_, false, 0.5f, 0.5f);
  
  engines_.RegisterInstance(&virtual_analog_engine_, false, 0.8f, 0.8f);
  engines_.RegisterInstance(&waveshaping_engine_, false, 0.7f, 0.6f);
  engines_.RegisterInstance(&fm_engine_, false, 0.6f, 0.6f);
  engines_.RegisterInstance(&grain_engine_, false, 0.7f, 0.6f);
  engines_.RegisterInstance(&additive_engine_, false, 0.8f, 0.8f);
  engines_.RegisterInstance(&wavetable_engine_, false, 0.6f, 0.6f);
  engines_.RegisterInstance(&chord_engine_, false, 0.8f, 0.8f);
  engines_.RegisterInstance(&speech_engine_, false, -0.7f, 0.8f);

  engines_.RegisterInstance(&swarm_engine_, false, -3.0f, 1.0f);
  engines_.RegisterInstance(&noise_engine_, false, -1.0f, -1.0f);
  engines_.RegisterInstance(&particle_engine_, false, -2.0f, 1.0f);
  engines_.RegisterInstance(&string_engine_, true, -1.0f, 0.8f);
  engines_.RegisterInstance(&modal_engine_, true, -1.0f, 0.8f);
  engines_.RegisterInstance(&bass_drum_engine_, true, 0.8f, 0.8f);
  engines_.RegisterInstance(&snare_drum_engine_, true, 0.8f, 0.8f);
  engines_.RegisterInstance(&hi_hat_engine_, true, 0.8f, 0.8f);
  
  for (int i = 0; i < engines_.size(); ++i) {
    // All engines will share the same RAM space.
    allocator->Free();
    engines_.get(i)->Init(allocator);
  }

  square_oscillator_.Init();
  sine_oscillator_.Init();
  
  engine_quantizer_.Init(engines_.size(), 0.05f, true);
  previous_engine_index_ = -1;
  reload_user_data_ = false;
  engine_cv_ = 0.0f;
  
  out_post_processor_.Init();
  aux_post_processor_.Init();

  decay_envelope_.Init();
  lpg_envelope_.Init();
  
  trigger_state_ = false;
  previous_note_ = 0.0f;
  
  trigger_delay_.Init(trigger_delay_line_);
}

void Voice::Render(
    const Patch& patch,
    const Modulations& modulations,
    Frame* frames,
    size_t size) {
  // Trigger, LPG, internal envelope.
      
  // Delay trigger by 1ms to deal with sequencers or MIDI interfaces whose
  // CV out lags behind the GATE out.
  trigger_delay_.Write(modulations.trigger);
  float trigger_value = trigger_delay_.Read(kTriggerDelay);
  bool trigger_high = trigger_value > 0.3f;

  float modulations_timbre = modulations.timbre;
  float modulations_morph = modulations.morph;
  float modulations_harmonics = modulations.harmonics;
  float modulations_level = modulations.level;
  float modulations_note = modulations.note;
  if (modulations.trigger_patched && (patch.hold_on_trigger_option == 1)) {
    if (!trigger_state_ && trigger_high) {
      held_timbre_ = modulations.timbre;
      held_morph_ = modulations.morph;
      held_harmo_ = modulations.harmonics;
      held_level_ = modulations.level;
      held_note_ = modulations.note;
    }
    modulations_timbre = held_timbre_;
    modulations_morph = held_morph_;
    modulations_harmonics = held_harmo_;
    modulations_level = held_level_;
    modulations_note = held_note_;
  }
  bool level_patched = modulations.level_patched;
  float patch_decay = patch.decay;
  if (patch.locked_frequency_pot_option == 1) {
    patch_decay = patch.freqlock_param;
  }
  if (modulations.trigger_patched && patch.level_cv_option == 1) {
    level_patched = false;
    patch_decay += modulations_level;
    modulations_level = 0.0f;
  }
  CONSTRAIN(patch_decay, 0.0f, 1.0f);

  float patch_lpg_colour = patch.lpg_colour;
  if (patch.model_cv_option == 1) {
    patch_lpg_colour += modulations.engine;
  }
  CONSTRAIN(patch_lpg_colour, 0.0f, 1.0f);
  
  bool previous_trigger_state = trigger_state_;
  if (!previous_trigger_state) {
    if (trigger_high) {
      trigger_state_ = true;
      if (!level_patched) {
        lpg_envelope_.Trigger();
      }
      decay_envelope_.Trigger();
      engine_cv_ = modulations.engine;
    }
  } else {
    if (trigger_value < 0.1f) {
      trigger_state_ = false;
    }
  }
  if (!modulations.trigger_patched) {
    engine_cv_ = modulations.engine;
  }

  // Engine selection.
  int engine_index = engine_quantizer_.Process(
      patch.engine,
      patch.model_cv_option == 0 ? engine_cv_ : 0.0f);
  
  Engine* e = engines_.get(engine_index);
  
  if (engine_index != previous_engine_index_ || reload_user_data_) {
    UserData user_data;
    const uint8_t* data = user_data.ptr(engine_index);
    if (!data && engine_index >= 2 && engine_index <= 4) {
      data = fm_patches_table[engine_index - 2];
    }
    e->LoadUserData(data);
    e->Reset();

    out_post_processor_.Reset();
    sine_oscillator_.Init();
    square_oscillator_.Init();
    previous_engine_index_ = engine_index;
    reload_user_data_ = false;
  }
  EngineParameters p;
  p.chord_set_option = patch.chord_set_option;

  bool rising_edge = trigger_state_ && !previous_trigger_state;
  float note = (modulations_note + previous_note_) * 0.5f;
  previous_note_ = modulations_note;
  const PostProcessingSettings& pp_s = e->post_processing_settings;

  if (modulations.trigger_patched) {
    p.trigger = (rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW) | \
      (trigger_state_ ? TRIGGER_HIGH : TRIGGER_LOW);
  } else {
    p.trigger = TRIGGER_UNPATCHED;
  }
  
  const float short_decay = (200.0f * kBlockSize) / kSampleRate *
      SemitonesToRatio(-96.0f * patch_decay);

  decay_envelope_.Process(short_decay * 2.0f);

  float compressed_level = 1.3f * modulations_level / (0.3f + fabsf(modulations_level));
  CONSTRAIN(compressed_level, 0.0f, 1.0f);
  p.accent = level_patched ? compressed_level : 0.8f;

  bool use_internal_envelope = modulations.trigger_patched;

  // Actual synthesis parameters.
  
  p.harmonics = patch.harmonics + modulations_harmonics;
  CONSTRAIN(p.harmonics, 0.0f, 1.0f);

  float internal_envelope_amplitude = 1.0f;
  float internal_envelope_amplitude_timbre = 1.0f;
  if (engine_index == 15) {
    internal_envelope_amplitude = 2.0f - p.harmonics * 6.0f;
    CONSTRAIN(internal_envelope_amplitude, 0.0f, 1.0f);
    speech_engine_.set_prosody_amount(
        !modulations.trigger_patched || modulations.frequency_patched ?
            0.0f : patch.frequency_modulation_amount);
    speech_engine_.set_speed( 
        !modulations.trigger_patched || modulations.morph_patched ?
            0.0f : patch.morph_modulation_amount);
  } else if (engine_index == 7) {
    if (modulations.trigger_patched && !modulations.timbre_patched) {
      // Disable internal envelope on TIMBRE, and enable the envelope generator
      // built into the chiptune engine.
      internal_envelope_amplitude_timbre = 0.0f;
      chiptune_engine_.set_envelope_shape(patch.timbre_modulation_amount);
    } else {
      chiptune_engine_.set_envelope_shape(ChiptuneEngine::NO_ENVELOPE);
    }
  }
  
  p.note = ApplyModulations(
      patch.note + note,
      patch.frequency_modulation_amount,
      modulations.frequency_patched,
      modulations.frequency,
      use_internal_envelope,
      internal_envelope_amplitude * \
          decay_envelope_.value() * decay_envelope_.value() * 48.0f,
      1.0f,
      -119.0f,
      120.0f);

  p.timbre = ApplyModulations(
      patch.timbre,
      patch.timbre_modulation_amount,
      modulations.timbre_patched,
      modulations_timbre,
      use_internal_envelope,
      internal_envelope_amplitude_timbre * decay_envelope_.value(),
      0.0f,
      0.0f,
      1.0f);

  p.morph = ApplyModulations(
      patch.morph,
      patch.morph_modulation_amount,
      modulations.morph_patched,
      modulations_morph,
      use_internal_envelope,
      internal_envelope_amplitude * decay_envelope_.value(),
      0.0f,
      0.0f,
      1.0f);

  bool already_enveloped = pp_s.already_enveloped;
  e->Render(p, out_buffer_, aux_buffer_, size, &already_enveloped);

  if (patch.aux_subosc_wave_option >= 1) {
    float frequency = NoteToFrequency(p.note);
    if (patch.aux_subosc_octave_option == 1) {
      frequency /= 2.0f;
    } else if (patch.aux_subosc_octave_option == 2) {
      frequency /= 4.0f;
    }

    if (patch.aux_subosc_wave_option == 1) {
      square_oscillator_.Render(frequency, aux_buffer_, size);
    } else if (patch.aux_subosc_wave_option == 2) {
      sine_oscillator_.Render(frequency, aux_buffer_, size);
    }
  }

  // Crossfade the aux output between main and aux models.
  bool use_locked_frequency_pot_for_aux_crossfade = patch.locked_frequency_pot_option == 2;
  bool use_model_cv_for_aux_crossfade = patch.model_cv_option == 2;
  bool use_aux_crossfade = use_locked_frequency_pot_for_aux_crossfade || \
    use_model_cv_for_aux_crossfade;
  if (use_aux_crossfade) {
    float aux_proportion = 0.5f;
    if (use_locked_frequency_pot_for_aux_crossfade) {
      aux_proportion = patch.freqlock_param;
    }
    if (use_model_cv_for_aux_crossfade) {
      aux_proportion += modulations.engine;
    }
    CONSTRAIN(aux_proportion, 0.0f, 1.0f);

    for (size_t i = 0; i < size; ++i) {
      aux_buffer_[i] = Crossfade(out_buffer_[i], aux_buffer_[i], aux_proportion);
    }
  }
  
  bool lpg_bypass = already_enveloped || \
      (!level_patched && !modulations.trigger_patched);
  bool aux_lpg_bypass = lpg_bypass || (patch.aux_subosc_wave_option != 0 && !use_aux_crossfade);
  
  // Compute LPG parameters.
  if (!lpg_bypass) {
    const float hf = patch_lpg_colour;
    const float decay_tail = (20.0f * kBlockSize) / kSampleRate *
        SemitonesToRatio(-72.0f * patch_decay + 12.0f * hf) - short_decay;
    
    if (level_patched) {
      lpg_envelope_.ProcessLP(compressed_level, short_decay, decay_tail, hf);
    } else {
      const float attack = NoteToFrequency(p.note) * float(kBlockSize) * 2.0f;
      lpg_envelope_.ProcessPing(attack, short_decay, decay_tail, hf);
    }
  } else {
    lpg_envelope_.Init();
  }
  
  out_post_processor_.Process(
      pp_s.out_gain,
      lpg_bypass,
      lpg_envelope_.gain(),
      lpg_envelope_.frequency(),
      lpg_envelope_.hf_bleed(),
      out_buffer_,
      &frames->out,
      size,
      2);

  aux_post_processor_.Process(
      pp_s.aux_gain,
      aux_lpg_bypass,
      lpg_envelope_.gain(),
      lpg_envelope_.frequency(),
      lpg_envelope_.hf_bleed(),
      aux_buffer_,
      &frames->aux,
      size,
      2);
}
  
}  // namespace plaits
