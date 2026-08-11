# Copyright 2016 Emilie Gillet.
# 
# Author: Emilie Gillet (emilie.o.gillet@gmail.com)
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
# 
# See http://creativecommons.org/licenses/MIT/ for more information.

# System specifications
F_CRYSTAL      = 8000000L
F_CPU          = 72000000L
SYSCLOCK       = SYSCLK_FREQ_72MHz
FAMILY         = f37x
# USB            = enabled

APPLICATION_LARGE = TRUE
BOOTLOADER        = plaits_bootloader

# Preferred upload command
UPLOAD_COMMAND  = upload_combo_jtag_erase_first

# Packages to build
TARGET         = plaits
PACKAGES       = plaits \
		plaits/drivers \
		plaits/dsp \
		plaits/dsp/fm \
		plaits/dsp/fx \
		plaits/dsp/chords \
		plaits/dsp/drum_modelling \
		plaits/dsp/engine \
		plaits/dsp/engine2 \
		plaits/dsp/physical_modelling \
		plaits/dsp/speech \
		stm_audio_bootloader/fsk \
		stmlib/dsp \
		stmlib/utils \
		stmlib/system
PACKAGES       += $(PLAITS_EXTRA_PACKAGES)
RESOURCES      = plaits/resources

TOOLCHAIN_PATH ?= /usr/local/arm-4.8.3/

include stmlib/makefile.inc

# Optimize control, startup, and callback glue for size while leaving the voice
# renderer and every synthesis engine at -O2. FillBuffer itself only dispatches
# to those hot paths, so this recovers flash without slowing their DSP loops.
$(BUILD_DIR)plaits.o \
$(BUILD_DIR)ui.o \
$(BUILD_DIR)settings.o \
$(BUILD_DIR)parameter_randomizer.o \
$(BUILD_DIR)user_data_receiver.o \
$(BUILD_DIR)audio_dac.o \
$(BUILD_DIR)cv_adc.o \
$(BUILD_DIR)firmware_update_adc.o \
$(BUILD_DIR)leds.o \
$(BUILD_DIR)pots_adc.o \
$(BUILD_DIR)switches.o \
$(BUILD_DIR)debug_port.o \
$(BUILD_DIR)packet_decoder.o \
$(BUILD_DIR)bootloader_utils.o \
$(BUILD_DIR)chord_bank.o \
$(BUILD_DIR)system_clock.o \
$(BUILD_DIR)system_stm32f37x.o: CCFLAGS += -Os

# The STM peripheral library is initialization and register-access glue, not
# synthesis DSP. Its default -O2 expansion is expensive on a 224 KB target;
# size optimization keeps the same hardware behavior and leaves the audio
# renderer and engine inner loops untouched.
$(BUILD_DIR)stm32f37x_%.o: CCFLAGS += -Os

# The hosted builder generates engine_config.h with the recipe's engine registry
# (PLAITS_ENGINE_MEMBERS, reached through voice.h) and its compile-time option
# macros (the PLAITS_BUILD_* / PLAITS_ENGINE_COUNT values that build_config.h
# otherwise defaults). Only the objects below read those macros — directly via
# build_config.h (settings, chord_bank, scale_voices, ui) or through voice.h
# (voice, plaits) — so force-including the generated config into just these
# keeps every other object recipe-independent and reusable from ccache across
# recipes.
#
# This list MUST cover every built .cc that reaches engine_config.h or
# build_config.h; a missing one would silently compile with default options
# instead of the recipe's. check_config_scope.py enforces that at build time.
# ENGINE_CONFIG is empty for ordinary firmware builds, which fall back to the
# checked-in default headers via each unit's own #include.
RECIPE_CONFIG_OBJS = voice.o plaits.o ui.o settings.o chord_bank.o scale_voices.o scale_wavetable_voices.o pots_adc.o switches.o sync_input.o audio_rate_timer.o cv_adc.o additive_engine.o chord_engine.o grain_engine.o speech_engine.o swarm_engine.o waveshaping_engine.o fm_engine.o vowel_fof_engine.o wavetable_engine.o virtual_analog_engine.o virtual_analog_dual_engine.o virtual_analog_crossfade_engine.o virtual_analog_vcf_engine.o wave_terrain_engine.o attractor_engine.o bass_drum_engine.o brass_engine.o blown_engine.o bowed_engine.o buzz_engine.o bytebeat_engine.o chiptune_engine.o clap_engine.o csaw_engine.o cymbal_engine.o diatonic_chord_engine.o digital_modulation_engine.o dual_sync_engine.o fluted_engine.o fold_engine.o formant_speech_engine.o freshets_formant_engine.o gendy_engine.o glisson_engine.o granular_cloud_engine.o harmonics_engine.o helix_engine.o hi_hat_engine.o kick_engine.o lockstep_engine.o loopback_engine.o lpc_speech_engine.o modal_engine.o morph_engine.o noise_engine.o noise_bank_engine.o particle_engine.o particle_burst_engine.o phase_distortion_engine.o phase_flock_engine.o phase_weave_engine.o plucked_engine.o pulsar_engine.o question_mark_engine.o raw_fm_engine.o reed_pipe_engine.o ring_mod_engine.o rulefield_engine.o saw_comb_engine.o saw_square_engine.o saw_swarm_engine.o scale_stack_engine.o scanned_engine.o shakers_engine.o sideband_engine.o six_op_engine.o snare_drum_engine.o snare_engine.o spectral_spiral_engine.o string_engine.o string_machine_engine.o struck_bell_engine.o struck_drum_engine.o sub_oscillator_engine.o tapfield_engine.o toy_engine.o triple_engine.o undertow_engine.o vosim_engine.o vowel_engine.o wave_paraphonic_engine.o wave_scan_engine.o wavetable_chord_engine.o wavetable_scale_stack_engine.o z_filter_engine.o
RECIPE_CONFIG_TARGETS = $(patsubst %,$(BUILD_DIR)%,$(RECIPE_CONFIG_OBJS))
ifneq ($(ENGINE_CONFIG),)
$(RECIPE_CONFIG_TARGETS): CPPFLAGS += -include $(ENGINE_CONFIG)
$(RECIPE_CONFIG_TARGETS): $(ENGINE_CONFIG)
# Exactly ONE object defines the swappable FM bank arrays; the rest see a
# declaration and skip the initializers. The single definition is what makes the
# region table meaningful — two per-unit copies would sit at different addresses,
# so UserData::Save (plaits.o) would erase and reprogram one while the engine
# (voice.o) kept reading the other, and a transfer would appear to do nothing.
# It also keeps six of the seven recipe-config objects from parsing up to 16 KB
# of packed patch data they never reference.
$(BUILD_DIR)voice.o: CPPFLAGS += -DPLAITS_ENGINE_CONFIG_OWNS_USER_DATA_BANKS
endif

# Speech-bank data is independent of the engine registry and can be much larger
# than the ordinary recipe config. Compile it once into the shared LPC
# controller; Original Speech and LPC Words reach it through small helpers.
ifneq ($(SPEECH_CONFIG),)
$(BUILD_DIR)lpc_speech_synth_controller.o: CPPFLAGS += -include $(SPEECH_CONFIG)
endif

# Compile the decoded-frame controller path only for recipes that carry a
# Speech-bank resource. The macro changes LPCSpeechSynthWordBank's class layout,
# so every object that lays out Voice, SpeechEngine, or LPCSpeechEngine must see
# the same value. Applying it only to the method implementations corrupts the
# following Voice members during startup even when the recipe has no raw banks.
# The split engines both use the shared custom-bank helpers, so their method
# implementations must see the same value too. Legacy/default palettes keep the
# stock code footprint by compiling all of these objects with the value 0.
SPEECH_LAYOUT_OBJS = voice.o plaits.o ui.o speech_engine.o lpc_speech_engine.o lpc_speech_synth_controller.o lpc_speech_synth_phonemes.o
ifneq ($(SPEECH_BANKS_ENABLED),)
$(patsubst %,$(BUILD_DIR)%,$(SPEECH_LAYOUT_OBJS)): CPPFLAGS += -DPLAITS_HAS_CUSTOM_SPEECH_BANKS=$(SPEECH_BANKS_ENABLED)
endif

# Per-engine stereo (OUT/AUX as an L/R pair) is gated at compile time by
# PLAITS_STEREO_<X> (default 1 in stereo_config.h). To disable an engine's
# stereo the hosted builder passes PLAITS_STEREO_<X>=0, and the rule below adds
# -DPLAITS_STEREO_<X>=0 to ONLY that engine's object — so each engine caches as
# just two variants (on/off) and the default (all-stereo) build's warm ccache is
# untouched (no flag is added when the value is 1/unset). <MACRO>:<object> pairs;
# the three DX7 banks share six_op_engine, hence SIX_OP.
PLAITS_STEREO_MODELS = \
  VIRTUAL_ANALOG:virtual_analog_engine WAVESHAPING:waveshaping_engine \
  VIRTUAL_ANALOG_DUAL:virtual_analog_dual_engine \
  VIRTUAL_ANALOG_CROSSFADE:virtual_analog_crossfade_engine \
  TWO_OP_FM:fm_engine GRANULAR_FORMANT:grain_engine HARMONIC:additive_engine \
  WAVETABLE:wavetable_engine CHORDS:chord_engine SPEECH:speech_engine \
  FORMANT_SPEECH:formant_speech_engine LPC_SPEECH:lpc_speech_engine \
  SWARM:swarm_engine FILTERED_NOISE:noise_engine PARTICLE_NOISE:particle_engine \
  INHARMONIC_STRING:string_engine MODAL_RESONATOR:modal_engine \
  ANALOG_BASS_DRUM:bass_drum_engine ANALOG_SNARE:snare_drum_engine \
  ANALOG_HI_HAT:hi_hat_engine VIRTUAL_ANALOG_VCF:virtual_analog_vcf_engine \
  WAVE_TERRAIN:wave_terrain_engine CHIPTUNE:chiptune_engine SIX_OP:six_op_engine \
  GLISSON:glisson_engine GENDY:gendy_engine SCANNED:scanned_engine \
  LOOPBACK:loopback_engine LOCKSTEP:lockstep_engine TAPFIELD:tapfield_engine \
  PHASE_WEAVE:phase_weave_engine SIDEBAND_BANK:sideband_engine \
  UNDERTOW:undertow_engine REED_PIPE:reed_pipe_engine \
  PHASE_FLOCK:phase_flock_engine RULEFIELD:rulefield_engine \
  TOY:toy_engine BOWED:bowed_engine CSAW:csaw_engine \
  RING_MOD:ring_mod_engine VOWEL_FOF:vowel_fof_engine \
  DIGITAL_MODULATION:digital_modulation_engine CLAP:clap_engine \
  FRESHETS_FORMANT:freshets_formant_engine
define _plaits_stereo_rule
$(BUILD_DIR)$(word 2,$(subst :, ,$(1))).o: CPPFLAGS += $$(if $$(filter 0,$$(PLAITS_STEREO_$(word 1,$(subst :, ,$(1))))),-DPLAITS_STEREO_$(word 1,$(subst :, ,$(1)))=0)
endef
$(foreach _m,$(PLAITS_STEREO_MODELS),$(eval $(call _plaits_stereo_rule,$(_m))))

# Rule for building the firmware update file
wav:  $(TARGET_BIN)
	python stm_audio_bootloader/qpsk/encoder.py \
		-t stm32f3 -k -s 48000 -b 12000 -c 6000 -p 256 \
		$(TARGET_BIN)

user_data_image:
	$(BASE_JTAG_CMD) \
		-c "dump_image plaits_user_data.bin 0x08007000 0x01000" \
		$(POSTLUDE)
