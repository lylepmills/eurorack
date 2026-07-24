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
  PLAITS_REGISTER_ENGINES(engines_);
  
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
  float macro_cv = 0.0f;
  if (modulations.trigger_patched && patch.level_cv_option == 1) {
    level_patched = false;
    patch_decay += modulations_level;
    modulations_level = 0.0f;
  }
  CONSTRAIN(patch_decay, 0.0f, 1.0f);

  float patch_lpg_colour = patch.lpg_colour;
  if (patch.model_cv_option == 1) {
    patch_lpg_colour += modulations.engine;
  } else if (patch.model_cv_option == 3) {
    // Repurpose the MODEL CV input as the fourth synthesis macro. Unlike the
    // LEVEL macro option this does not require TRIG to be patched, since the
    // MODEL input never drives the internal VCA.
    macro_cv = modulations.engine;
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
#if PLAITS_HAS_USER_DATA_BANK
    if (!data && kEngineUserDataBank[engine_index] >= 0) {
      const int bank = kEngineUserDataBank[engine_index];
#if PLAITS_HAS_USER_DATA_BANK_OVERRIDE
      // A recipe-baked custom bank replaces the built-in default for this bank;
      // a runtime TIMBRE-loaded user bank (above) still takes precedence.
      data = kUserDataBankOverride[bank]
          ? kUserDataBankOverride[bank]
          : fm_patches_table[bank];
#else
      data = fm_patches_table[bank];
#endif  // PLAITS_HAS_USER_DATA_BANK_OVERRIDE
    }
#endif  // PLAITS_HAS_USER_DATA_BANK
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
  // Aux output mode 3 requests a true stereo render (OUT = left, AUX = right)
  // from engines that support it; the others fall back to their regular aux
  // output. stereo_capable() is compile-time false for an engine built with its
  // PLAITS_STEREO_<X> flag off, so stereo is never routed to it.
  const bool stereo_render = patch.aux_subosc_wave_option == 3 &&
      e->stereo_capable();
  p.stereo = stereo_render;
  p.macro = patch.locked_frequency_pot_option == 3
      ? patch.freqlock_param
      : 0.5f;
  p.macro += macro_cv;
  CONSTRAIN(p.macro, 0.0f, 1.0f);

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
#if PLAITS_HAS_SPEECH_ENGINE
  if (kSpeechEngineMask & (1u << engine_index)) {
    internal_envelope_amplitude = 2.0f - p.harmonics * 6.0f;
    CONSTRAIN(internal_envelope_amplitude, 0.0f, 1.0f);
    speech_engine_.set_prosody_amount(
        !modulations.trigger_patched || modulations.frequency_patched ?
            0.0f : patch.frequency_modulation_amount);
    speech_engine_.set_speed( 
        !modulations.trigger_patched || modulations.morph_patched ?
            0.0f : patch.morph_modulation_amount);
  }
#endif
#if PLAITS_HAS_CHIPTUNE_ENGINE
  if (kChiptuneEngineMask & (1u << engine_index)) {
    if (modulations.trigger_patched && !modulations.timbre_patched) {
      // Disable internal envelope on TIMBRE, and enable the envelope generator
      // built into the chiptune engine.
      internal_envelope_amplitude_timbre = 0.0f;
      chiptune_engine_.set_envelope_shape(patch.timbre_modulation_amount);
    } else {
      chiptune_engine_.set_envelope_shape(ChiptuneEngine::NO_ENVELOPE);
    }
  }
#endif
  
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

  const bool aux_replaced_by_subosc = patch.aux_subosc_wave_option == 1 ||
      patch.aux_subosc_wave_option == 2;
  if (aux_replaced_by_subosc) {
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
  bool aux_lpg_bypass = lpg_bypass || (aux_replaced_by_subosc && !use_aux_crossfade);
  
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
      // A stereo pair must leave with the same gain on both channels.
      stereo_render ? pp_s.out_gain : pp_s.aux_gain,
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
