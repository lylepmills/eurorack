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

# These paths do not run in the audio callback. Optimizing them for size leaves
# the synthesis code at -O2 while recovering flash for alternate engine banks.
$(BUILD_DIR)ui.o \
$(BUILD_DIR)settings.o \
$(BUILD_DIR)user_data_receiver.o: CCFLAGS += -Os

# The hosted builder generates engine_config.h with the recipe's engine registry
# (PLAITS_ENGINE_MEMBERS, reached through voice.h) and its compile-time option
# macros (the PLAITS_BUILD_* / PLAITS_ENGINE_COUNT values that build_config.h
# otherwise defaults). Only the objects below read those macros — directly via
# build_config.h (settings, chord_bank, ui) or through voice.h (voice, plaits) —
# so force-including the generated config into just these keeps every other
# object recipe-independent and reusable from ccache across recipes.
#
# This list MUST cover every built .cc that reaches engine_config.h or
# build_config.h; a missing one would silently compile with default options
# instead of the recipe's. check_config_scope.py enforces that at build time.
# ENGINE_CONFIG is empty for ordinary firmware builds, which fall back to the
# checked-in default headers via each unit's own #include.
RECIPE_CONFIG_OBJS = voice.o plaits.o ui.o settings.o chord_bank.o
ifneq ($(ENGINE_CONFIG),)
$(patsubst %,$(BUILD_DIR)%,$(RECIPE_CONFIG_OBJS)): CPPFLAGS += -include $(ENGINE_CONFIG)
endif

# Rule for building the firmware update file
wav:  $(TARGET_BIN)
	python stm_audio_bootloader/qpsk/encoder.py \
		-t stm32f3 -k -s 48000 -b 12000 -c 6000 -p 256 \
		$(TARGET_BIN)

user_data_image:
	$(BASE_JTAG_CMD) \
		-c "dump_image plaits_user_data.bin 0x08007000 0x01000" \
		$(POSTLUDE)
