# Builds one Plaits Lab package into a VCV Rack 2 module: a test bench for
# patching a package without flashing hardware.
#
#   make RACK_DIR=<Rack-SDK> PACKAGE=<package directory | catalog id> dist
#   make RACK_DIR=<Rack-SDK> PACKAGE=<...> test   # Rack-free checks of the voice
#   make RACK_DIR=<Rack-SDK> clean
RACK_DIR ?= ../Rack-SDK
PYTHON ?= python3

ifneq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
ifeq ($(PACKAGE),)
$(error Set PACKAGE to a package directory or a built-in catalog id, e.g. PACKAGE=chords)
endif
# The SDK resolves the package's sources, engine class and post-processing. The
# fragment is rewritten only when it changes, and everything built depends on
# it, so switching PACKAGE rebuilds without a clean.
$(shell mkdir -p build; $(PYTHON) sources.py "$(PACKAGE)" > build/package.mk.new; \
  cmp -s build/package.mk.new build/package.mk || mv build/package.mk.new build/package.mk; \
  rm -f build/package.mk.new)
include build/package.mk
ifeq ($(PLAITS_LAB_SOURCES),)
$(error sources.py could not resolve PACKAGE=$(PACKAGE))
endif
# Keyed by the fragment, so a package switch never reuses another's test binary,
# even within the one-second timestamp resolution of older makes.
TEST_BIN := build/test_lab_voice-$(firstword $(shell cksum build/package.mk))
endif

FLAGS += -DTEST -D_USE_MATH_DEFINES $(PLAITS_LAB_FLAGS)
SOURCES += src/plugin.cpp src/lab_voice.cc $(PLAITS_LAB_SOURCES)
DISTRIBUTABLES += res LICENSE

include $(RACK_DIR)/plugin.mk

$(OBJECTS): build/package.mk

test: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): tests/test_lab_voice.cc src/lab_voice.cc $(PLAITS_LAB_SOURCES)
	$(CXX) -std=c++11 -O2 -DTEST -D_USE_MATH_DEFINES $(PLAITS_LAB_FLAGS) -Isrc $^ -o $@

.PHONY: test
