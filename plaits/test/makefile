PACKAGES       = plaits/test stmlib/utils plaits plaits/dsp plaits/dsp/chords plaits/dsp/engine plaits/dsp/engine2 plaits/dsp/fm stmlib/dsp plaits/dsp/speech plaits/dsp/physical_modelling stm_audio_bootloader/fsk

VPATH          = $(PACKAGES)

TARGET         = plaits_test
BUILD_ROOT     = build/
BUILD_DIR      = $(BUILD_ROOT)$(TARGET)/
HOST_ARCH      = $(shell uname -m)
HOST_OS        = $(shell uname -s)
ifeq ($(HOST_ARCH),x86_64)
HOST_ARCH_FLAGS = -msse2
endif
ifeq ($(HOST_OS),Darwin)
HOST_LINK_FLAGS = -Wl,-no_pie -L/opt/local/lib
else
HOST_LINK_FLAGS = -no-pie
endif
CC_FILES       = algorithms.cc \
		additive_engine.cc \
		attractor_engine.cc \
		bass_drum_engine.cc \
		chiptune_engine.cc \
		chord_bank.cc \
		chord_engine.cc \
		dx_units.cc \
		fm_engine.cc \
		gendy_engine.cc \
		grain_engine.cc \
		glisson_engine.cc \
		hi_hat_engine.cc \
		lockstep_engine.cc \
		loopback_engine.cc \
		lpc_speech_synth.cc \
		lpc_speech_synth_controller.cc \
		lpc_speech_synth_phonemes.cc \
		lpc_speech_synth_words.cc \
		modal_engine.cc \
		modal_voice.cc \
		naive_speech_synth.cc \
		noise_engine.cc \
		packet_decoder.cc \
		particle_engine.cc \
		phase_flock_engine.cc \
		phase_distortion_engine.cc \
		phase_weave_engine.cc \
		plaits_test.cc \
		pulsar_engine.cc \
		random.cc \
		reed_pipe_engine.cc \
		resonator.cc \
		resources.cc \
		rulefield_engine.cc \
		sam_speech_synth.cc \
		scanned_engine.cc \
		sideband_engine.cc \
		six_op_engine.cc \
		snare_drum_engine.cc \
		spectral_spiral_engine.cc \
		speech_engine.cc \
		string.cc \
		string_engine.cc \
		string_machine_engine.cc \
		string_voice.cc \
		swarm_engine.cc \
		tapfield_engine.cc \
		undertow_engine.cc \
		units.cc \
		user_data_receiver.cc \
		virtual_analog_engine.cc \
		virtual_analog_vcf_engine.cc \
		voice.cc \
		waveshaping_engine.cc \
		wavetable_engine.cc \
		wave_terrain_engine.cc
OBJ_FILES      = $(CC_FILES:.cc=.o)
OBJS           = $(patsubst %,$(BUILD_DIR)%,$(OBJ_FILES)) $(STARTUP_OBJ)
DEPS           = $(OBJS:.o=.d)
DEP_FILE       = $(BUILD_DIR)depends.mk

all:  plaits_test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)%.o: %.cc
	g++ -c -DTEST -g -Wall -Werror $(HOST_ARCH_FLAGS) -Wno-unused-variable -Wno-unused-local-typedefs -Wno-deprecated-declarations -O2 $(CXXFLAGS) -I. $< -o $@

$(BUILD_DIR)%.d: %.cc
	g++ -MM -DTEST -I. $< -MF $@ -MT $(@:.d=.o)

plaits_test:  $(OBJS)
	g++ -g -o $(TARGET) $(OBJS) $(HOST_LINK_FLAGS) $(CXXFLAGS) -lm

depends:  $(DEPS)
	cat $(DEPS) > $(DEP_FILE)

$(DEP_FILE):  $(BUILD_DIR) $(DEPS)
	cat $(DEPS) > $(DEP_FILE)

profile:	plaits_test
	env CPUPROFILE_FREQUENCY=1000 CPUPROFILE=$(BUILD_DIR)/plaits.prof ./plaits_test && pprof --pdf ./plaits_test $(BUILD_DIR)/plaits.prof > profile.pdf && open profile.pdf
	
clean:
	rm $(BUILD_DIR)*.*

include $(DEP_FILE)
