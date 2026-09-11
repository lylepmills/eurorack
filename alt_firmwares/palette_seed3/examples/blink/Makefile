# Seed3 blink under the Daisy bootloader (libDaisy's BootloaderBlink example,
# verbatim). Build: make [APP_TYPE=BOOT_QSPI|BOOT_SRAM|BOOT_NONE]
#   BOOT_NONE  -> internal flash via the ST ROM DFU (hold BOOT, tap RESET):  make program-dfu
#   BOOT_SRAM/BOOT_QSPI -> needs the Daisy bootloader (make program-boot once), then
#                          enter it (tap RESET, then BOOT within 2 s) and: make program-dfu
TARGET = blink
APP_TYPE ?= BOOT_QSPI
CPP_SOURCES = blink.cpp
LIBDAISY_DIR = ../../libDaisy
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
