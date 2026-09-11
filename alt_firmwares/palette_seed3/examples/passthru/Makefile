# Seed3 audio pass-through (libDaisy's AudioPassthru example, verbatim): the
# TAC5242 in hardware-default mode, IN L/R -> OUT L/R at 48 kHz / 48 frames.
# Build: make [APP_TYPE=BOOT_QSPI|BOOT_SRAM|BOOT_NONE]   (see ../blink/Makefile)
TARGET = passthru
APP_TYPE ?= BOOT_QSPI
CPP_SOURCES = passthru.cpp
LIBDAISY_DIR = ../../libDaisy
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
