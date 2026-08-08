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

#if defined(PLAITS_RANDOMIZER_PROFILES) && \
    defined(PLAITS_ENGINE_RANDOMIZER_PROFILE_INDICES)
static const ParameterRandomizerProfile kRandomizerProfiles[] =
    PLAITS_RANDOMIZER_PROFILES;
static const EngineRandomizerProfile kEngineRandomizerProfiles[] =
    PLAITS_ENGINE_RANDOMIZER_PROFILE_INDICES;
#else
static const ParameterRandomizerProfile kRandomizerProfiles[] = {
  { 0.25f, 0.55f, 0.000009f, 0.000006f, 0.60f },
};
#endif

#if PLAITS_BUILD_LINEAR_TZFM
// The FM input conditioner has a -0.2 V/V slope (100k input, 20k feedback).
// In SDADC single-ended zero-reference mode, normalized code has a 2 / 3.3 V
// slope. The stored Plaits FM calibration then multiplies by -60, yielding:
//
//   60 * 0.2 * 2 / 3.3 = 80 / 11 calibration units per input volt.
//
// Keep this hardware-derived constant explicit: treating the calibrated value
// as 12 semitones/volt under-scales linear FM to roughly 606 Hz/V.
static const float kFmCalibrationUnitsPerVolt = 80.0f / 11.0f;
static const float kLinearTzfmHzPerVolt = 1000.0f;
#endif

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
#if PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE
  one_knob_envelope_.Init();
  one_knob_envelope_active_ = false;
  one_knob_envelope_mode_ = OneKnobEnvelope::MODE_TRIGGERED;
#endif
  
  trigger_state_ = false;
  previous_note_ = 0.0f;
  parameter_randomizer_.Init();
  previous_attenuverter_mode_ = ATTENUVERTER_MODE_STOCK;
  
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
  // Resolve the trigger and selected engine before deciding what Auto does
  // with LEVEL. Model CV is sampled on the same edge, so the routing decision
  // follows the engine that will actually sound, not merely the panel setting.
  bool previous_trigger_state = trigger_state_;
  if (!previous_trigger_state) {
    if (trigger_high) {
      trigger_state_ = true;
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
  const PostProcessingSettings& pp_s = e->post_processing_settings;

  // LEVEL option 2 is Auto: feed the outer envelope's decay on ordinary
  // oscillator engines, but preserve LEVEL as accent/velocity whenever the
  // engine owns its amplitude envelope. Speech and Chiptune declare that
  // dynamically while rendering, so their behavior masks supplement the
  // registry's static already_enveloped flag.
  bool auto_preserves_level = pp_s.already_enveloped;
#if PLAITS_HAS_SPEECH_ENGINE
  auto_preserves_level = auto_preserves_level || \
      (kSpeechEngineMask & (1u << engine_index));
#endif
#if PLAITS_HAS_LPC_WORDS_ENGINE
  auto_preserves_level = auto_preserves_level || \
      (kLPCWordsEngineMask & (1u << engine_index));
#endif
#if PLAITS_HAS_CHIPTUNE_ENGINE
  auto_preserves_level = auto_preserves_level || \
      (kChiptuneEngineMask & (1u << engine_index));
#endif

  bool level_patched = modulations.level_patched;
#if PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE
  const bool one_knob_envelope_mode =
      patch.locked_frequency_pot_option == 4 ||
      patch.locked_frequency_pot_option == 5;
  const bool resonator_envelope_mode =
#if PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE && \
    defined(PLAITS_RESONATOR_ENVELOPE_ENGINE_MASK)
      one_knob_envelope_mode &&
      (PLAITS_RESONATOR_ENVELOPE_ENGINE_MASK & (1u << engine_index));
#else
      false;
#endif
#endif
  float patch_decay = patch.decay;
  if (patch.locked_frequency_pot_option == 3) {
    patch_decay = patch.freqlock_param;
  }
  const bool level_cv_to_decay = modulations.trigger_patched && \
      (patch.level_cv_option == 1 || \
       (patch.level_cv_option == 2 && !auto_preserves_level));
  if (level_cv_to_decay) {
    level_patched = false;
    patch_decay += modulations_level;
    modulations_level = 0.0f;
  }
  CONSTRAIN(patch_decay, 0.0f, 1.0f);

  const bool rising_edge = trigger_state_ && !previous_trigger_state;
  if (rising_edge && !level_patched) {
#if PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE
    // The alternate envelope supplies the LPG's input level directly. Stock
    // mode keeps the original vactrol ping.
    if (!one_knob_envelope_mode) {
      lpg_envelope_.Trigger();
    }
#else
    lpg_envelope_.Trigger();
#endif
  }
  if (patch.attenuverter_mode != previous_attenuverter_mode_) {
    if (patch.attenuverter_mode == ATTENUVERTER_MODE_STEP) {
      parameter_randomizer_.Trigger();
    }
    previous_attenuverter_mode_ = patch.attenuverter_mode;
  }
  if (rising_edge && patch.attenuverter_mode == ATTENUVERTER_MODE_STEP) {
    parameter_randomizer_.Trigger();
  }

  float macro_cv = 0.0f;
  float patch_lpg_colour = patch.lpg_colour;
  if (patch.model_cv_option == 3) {
    patch_lpg_colour += modulations.engine;
  } else if (patch.model_cv_option == 1) {
    // Repurpose the MODEL CV input as the fourth synthesis macro. Unlike the
    // LEVEL macro option this does not require TRIG to be patched, since the
    // MODEL input never drives the internal VCA.
    macro_cv = modulations.engine;
  }
  CONSTRAIN(patch_lpg_colour, 0.0f, 1.0f);

#if PLAITS_BUILD_LINEAR_TZFM
  // The FM attenuverter controls a fixed Hz/volt slope instead of a semitone
  // span for engines that opt in. Convert the hardware-calibrated FM value back
  // to volts, then apply a nominal 1 kHz/V slope. A fixed Hz slope is what makes
  // this linear FM; dividing by the carrier here would turn it back into
  // exponential/ratio modulation.
  float linear_fm[kMaxBlockSize];
  const bool use_linear_tzfm =
      modulations.frequency_patched && e->linear_tzfm_capable();
  if (use_linear_tzfm) {
    float amount = patch.frequency_modulation_amount;
    amount *= std::max(fabsf(amount) - 0.05f, 0.05f);
    amount *= 1.05f;
    const float scale =
        amount *
        (kLinearTzfmHzPerVolt / kFmCalibrationUnitsPerVolt) /
        kCorrectedSampleRate;
    for (size_t i = 0; i < size; ++i) {
      linear_fm[i] = modulations.frequency_audio[i] * scale;
    }
  }
#else
  const bool use_linear_tzfm = false;
#endif

  if (engine_index != previous_engine_index_ || reload_user_data_) {
    if (patch.attenuverter_mode == ATTENUVERTER_MODE_STEP) {
      parameter_randomizer_.Trigger();
    }
    UserData user_data;
    const uint8_t* data = user_data.ptr(engine_index);
    // Number of valid packed patch bytes behind `data`. Every built-in factory
    // bank is a full 32-patch bank; a recipe-baked custom bank may be shorter,
    // in which case its length comes from the generated size table, and a
    // TIMBRE-transferred bank declares its own count (below). LoadUserData sizes
    // the engine's Harmonics quantizer to length / SYX_SIZE, so a short bank
    // sweeps only its own patches.
    // A transferred bank declares its own patch count (UserData::bank_length),
    // so a short pick list sent over TIMBRE gives that many HARMONICS steps
    // rather than repeating to fill 32.
    size_t data_length = UserData::bank_length(data);
#if PLAITS_HAS_USER_DATA_BANK
    if (!data && kEngineUserDataBank[engine_index] >= 0) {
      const int bank = kEngineUserDataBank[engine_index];
#if PLAITS_HAS_RESOLVED_USER_DATA_BANK
      // The generator resolved every bank to its data pointer at build time: a
      // recipe-baked custom bank, a live stock factory bank (syx_bank_N), or NULL
      // for a bank no slot reaches. This build never NAMES fm_patches_table, so
      // --gc-sections drops the factory blobs of any customized-away / absent
      // banks. A runtime TIMBRE-loaded user bank (above) still takes precedence.
      // kResolvedUserDataBankSize carries each bank's real byte length, so a
      // short custom bank (< 32 patches) sizes the engine's Harmonics quantizer
      // to its own count; stock banks resolve to the full 32-patch length.
      data = kResolvedUserDataBank[bank];
      data_length = kResolvedUserDataBankSize[bank];
#else
      // Stock / default firmware: fall through to the built-in factory banks.
      data = fm_patches_table[bank];
#endif  // PLAITS_HAS_RESOLVED_USER_DATA_BANK
    }
#endif  // PLAITS_HAS_USER_DATA_BANK
    e->LoadUserData(data, data_length);
    e->Reset();

    out_post_processor_.Reset();
    sine_oscillator_.Init();
    square_oscillator_.Init();
    previous_engine_index_ = engine_index;
    reload_user_data_ = false;
  }
  EngineParameters p;
  p.chord_set_option = patch.chord_set_option;
#if PLAITS_BUILD_ENABLE_SYNC_INPUT
  p.hard_sync = patch.model_cv_option == 4 ? modulations.hard_sync : 0;
#else
  p.hard_sync = 0;
#endif
  // The stereo aux mode requests a true stereo render (OUT = left, AUX = right)
  // from engines that support it; the others fall back to their regular aux
  // output. stereo_capable() is compile-time false for an engine built with its
  // PLAITS_STEREO_<X> flag off, so stereo is never routed to it.
  const bool stereo_render = patch.aux_is_stereo() && e->stereo_capable();
  p.stereo = stereo_render;
#if PLAITS_BUILD_LINEAR_TZFM
  p.linear_fm = use_linear_tzfm ? linear_fm : NULL;
#endif
  p.macro = patch.locked_frequency_pot_option == 1
      ? patch.freqlock_param
      : 0.5f;
  p.macro += macro_cv;
  CONSTRAIN(p.macro, 0.0f, 1.0f);

  float note = (modulations_note + previous_note_) * 0.5f;
  previous_note_ = modulations_note;

  if (modulations.trigger_patched) {
    p.trigger = (rising_edge ? TRIGGER_RISING_EDGE : TRIGGER_LOW) | \
      (trigger_state_ ? TRIGGER_HIGH : TRIGGER_LOW);
  } else {
    p.trigger = TRIGGER_UNPATCHED;
  }
  
  const float short_decay = (200.0f * kBlockSize) / kSampleRate *
      SemitonesToRatio(-96.0f * patch_decay);

  decay_envelope_.Process(short_decay * 2.0f);

  // Keep Plaits' original short decay envelope as the modulation source for
  // synthesis parameters.  The alternate contour is an articulation source:
  // letting its multi-second shapes replace this value made even small saved
  // FM attenuverter offsets sound like prominent pitch envelopes.
  float internal_envelope = decay_envelope_.value();
#if PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE
  float articulation_envelope = internal_envelope;
  // The option is meaningful only with TRIG patched. Reset it when inactive so
  // selecting the mode later never revives an envelope from an old note. If it
  // is selected while a gate is already high, start an attack immediately
  // rather than waiting for a second gate cycle.
  const bool use_one_knob_envelope =
      one_knob_envelope_mode && modulations.trigger_patched;
  const OneKnobEnvelope::Mode contour_mode =
      patch.locked_frequency_pot_option == 4
          ? OneKnobEnvelope::MODE_TRIGGERED
          : OneKnobEnvelope::MODE_GATED;
  const bool contour_mode_changed =
      one_knob_envelope_active_ && contour_mode != one_knob_envelope_mode_;
  if (contour_mode_changed) {
    one_knob_envelope_.Init();
  }
  const bool start_one_knob_envelope =
      rising_edge ||
      (use_one_knob_envelope &&
       (!one_knob_envelope_active_ || contour_mode_changed) &&
       trigger_state_);
  if (use_one_knob_envelope) {
    articulation_envelope = one_knob_envelope_.Process(
        patch.freqlock_param,
        trigger_state_,
        start_one_knob_envelope,
        resonator_envelope_mode
            ? OneKnobEnvelope::PROFILE_ELEMENTS_RESONATOR
            : OneKnobEnvelope::PROFILE_SYNTH,
        contour_mode);
  } else if (one_knob_envelope_active_) {
    one_knob_envelope_.Init();
  }
  one_knob_envelope_active_ = use_one_knob_envelope;
  one_knob_envelope_mode_ = contour_mode;
#endif

  float compressed_level = 1.3f * modulations_level / (0.3f + fabsf(modulations_level));
  CONSTRAIN(compressed_level, 0.0f, 1.0f);
  p.accent = level_patched ? compressed_level : 0.8f;
#if PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE
  p.articulation_envelope = articulation_envelope;
  p.articulation_envelope_active =
      use_one_knob_envelope && resonator_envelope_mode &&
      one_knob_envelope_.active();
#endif

  bool use_internal_envelope = modulations.trigger_patched;
#if PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE
  // In contour mode the unpatched FM attenuverter must not turn articulation
  // into pitch. Patched FM remains fully functional because external
  // modulation takes precedence in ApplyModulations(). Timbre and morph keep
  // Plaits' original short internal modulation envelope.
  const bool use_internal_frequency_envelope =
      use_internal_envelope && !use_one_knob_envelope;
#else
  const bool use_internal_frequency_envelope = use_internal_envelope;
#endif

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
#if PLAITS_HAS_LPC_WORDS_ENGINE
  if (kLPCWordsEngineMask & (1u << engine_index)) {
    // Match stock Speech's word-bank region: the FM attenuverter controls the
    // recorded pitch contour when TRIG is patched and FM is not. Suppress the
    // ordinary trigger-driven pitch envelope so this remains a single-purpose
    // gesture; patched FM still modulates pitch normally.
    internal_envelope_amplitude = 0.0f;
    lpc_speech_engine_.set_prosody_amount(
        !modulations.trigger_patched || modulations.frequency_patched ?
            0.0f : patch.frequency_modulation_amount);
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

  const bool preserve_chiptune_timbre =
#if PLAITS_HAS_CHIPTUNE_ENGINE
      (kChiptuneEngineMask & (1u << engine_index)) &&
      modulations.trigger_patched && !modulations.timbre_patched;
#else
      false;
#endif
  const bool preserve_speech_morph =
#if PLAITS_HAS_SPEECH_ENGINE
      (kSpeechEngineMask & (1u << engine_index)) &&
      modulations.trigger_patched && !modulations.morph_patched;
#else
      false;
#endif
  const bool randomize_timbre =
      patch.attenuverter_mode != ATTENUVERTER_MODE_STOCK &&
      !modulations.timbre_patched && !preserve_chiptune_timbre;
  const bool randomize_morph =
      patch.attenuverter_mode != ATTENUVERTER_MODE_STOCK &&
      !modulations.morph_patched && !preserve_speech_morph;
  
  // Once the FM jack is routed to an engine's linear input, the attenuverter
  // must not also fall through to Plaits' unpatched fine-tune/internal pitch
  // envelope behavior. That fallback is selected by `use_external_modulation`
  // being false, so explicitly zero its amount for the linear path.
  const float note_modulation_amount =
      use_linear_tzfm ? 0.0f : patch.frequency_modulation_amount;
  p.note = ApplyModulations(
      patch.note + note,
      note_modulation_amount,
      modulations.frequency_patched && !use_linear_tzfm,
      modulations.frequency,
      use_internal_frequency_envelope,
      internal_envelope_amplitude * \
          internal_envelope * internal_envelope * 48.0f,
      1.0f,
      -119.0f,
      120.0f);

  p.timbre = ApplyModulations(
      patch.timbre,
      patch.timbre_modulation_amount,
      modulations.timbre_patched || randomize_timbre,
      modulations_timbre,
      use_internal_envelope,
      internal_envelope_amplitude_timbre * internal_envelope,
      0.0f,
      0.0f,
      1.0f);

  p.morph = ApplyModulations(
      patch.morph,
      patch.morph_modulation_amount,
      modulations.morph_patched || randomize_morph,
      modulations_morph,
      use_internal_envelope,
      internal_envelope_amplitude * internal_envelope,
      0.0f,
      0.0f,
      1.0f);

#if defined(PLAITS_RANDOMIZER_PROFILES) && \
    defined(PLAITS_ENGINE_RANDOMIZER_PROFILE_INDICES)
  const EngineRandomizerProfile& randomizer_profile =
      kEngineRandomizerProfiles[engine_index];
#else
  const EngineRandomizerProfile randomizer_profile = { 0, 0 };
#endif
  parameter_randomizer_.Process(
      static_cast<AttenuverterMode>(patch.attenuverter_mode),
      randomize_timbre,
      randomize_morph,
      patch.timbre,
      patch.morph,
      patch.timbre_modulation_amount,
      patch.morph_modulation_amount,
      kRandomizerProfiles[randomizer_profile.timbre],
      kRandomizerProfiles[randomizer_profile.morph],
      &p.timbre,
      &p.morph);

  bool already_enveloped = pp_s.already_enveloped;
  RenderEngineWithHardSync(
      e, p, out_buffer_, aux_buffer_, size, &already_enveloped);

#if PLAITS_HAS_CHIPTUNE_ENGINE
  // Clocked Chiptune bypasses the outer LPG because it owns its note envelope,
  // but historically ignored EngineParameters::accent too, leaving a patched
  // LEVEL jack dead. Restore its VCA role without putting the signal through a
  // second LPG: scale both of the engine's real outputs here. A suboscillator,
  // when selected below, deliberately replaces AUX after this stage and keeps
  // its existing envelope-bypass behavior.
  if (already_enveloped && level_patched && \
      (kChiptuneEngineMask & (1u << engine_index))) {
    for (size_t i = 0; i < size; ++i) {
      out_buffer_[i] *= compressed_level;
      aux_buffer_[i] *= compressed_level;
    }
  }
#endif

  if (patch.aux_is_subosc()) {
    float frequency = NoteToFrequency(p.note);
    const int octaves_down = patch.aux_subosc_octaves_down();
    if (octaves_down == 1) {
      frequency /= 2.0f;
    } else if (octaves_down == 2) {
      frequency /= 4.0f;
    }

    if (patch.aux_subosc_is_sine()) {
      sine_oscillator_.Render(frequency, aux_buffer_, size);
    } else {
      square_oscillator_.Render(frequency, aux_buffer_, size);
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
  bool aux_lpg_bypass = lpg_bypass || (patch.aux_is_subosc() && !use_aux_crossfade);
  
  // Compute LPG parameters.
  if (!lpg_bypass) {
    const float hf = patch_lpg_colour;
    const float decay_tail = (20.0f * kBlockSize) / kSampleRate *
        SemitonesToRatio(-72.0f * patch_decay + 12.0f * hf) - short_decay;
    
    if (level_patched) {
      lpg_envelope_.ProcessLP(compressed_level, short_decay, decay_tail, hf);
#if PLAITS_BUILD_ENABLE_ONE_KNOB_ENVELOPE
    } else if (use_one_knob_envelope) {
      // The contour already supplies the complete amplitude trajectory. Track
      // it directly instead of adding Plaits' stock vactrol decay afterward;
      // otherwise the advertised millisecond decays remain audibly long. The
      // LPG's level-dependent cutoff and COLOUR response are still retained.
      lpg_envelope_.ProcessLP(
          articulation_envelope, 1.0f, 0.0f, hf);
#endif
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
