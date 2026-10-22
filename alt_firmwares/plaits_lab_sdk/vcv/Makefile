# One package per build. `test` needs a C++ compiler, but no Rack SDK.
# make PACKAGE=chords test
# make RACK_DIR=<Rack-SDK> PACKAGE=<package directory | catalog id> dist
RACK_DIR ?= ../Rack-SDK
PYTHON ?= python3
SOURCE_TOOL ?= sources.py
.DEFAULT_GOAL := all

ifneq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
ifeq ($(PACKAGE),)
$(error Set PACKAGE to a package directory or a built-in catalog id, e.g. PACKAGE=chords)
endif
GENERATED := $(shell mkdir -p build && $(PYTHON) "$(SOURCE_TOOL)" "$(PACKAGE)" --prepare build/package > build/package.mk.new && echo ok)
ifneq ($(strip $(GENERATED)),ok)
$(error sources.py could not resolve PACKAGE=$(PACKAGE))
endif
$(shell cmp -s build/package.mk.new build/package.mk || mv build/package.mk.new build/package.mk)
include build/package.mk
endif

# External paths appear only in quoted include directives/flags. Rack's make
# rules split SOURCES on whitespace, so they receive local generated wrappers.
FLAGS += -DTEST -D_USE_MATH_DEFINES $(PLAITS_LAB_FLAGS)
SOURCES += $(PLAITS_LAB_PLUGIN_SOURCE) $(PLAITS_LAB_VOICE_SOURCE) $(PLAITS_LAB_SOURCES)
DISTRIBUTABLES += res LICENSE

ifneq ($(filter-out test clean,$(or $(MAKECMDGOALS),all)),)
include $(RACK_DIR)/plugin.mk
# Cached objects are package-specific, but the final plugin has one filename.
# Relink even when switching back to an older, already-built package.
.PHONY: $(TARGET)
else
clean:
	rm -rf build plugin.dylib plugin.so plugin.dll dist
endif

TEST_FLAGS := -std=c++11 -O2 -DTEST -D_USE_MATH_DEFINES -MMD -MP
VOICE_OBJECTS := $(addprefix build/tests/,$(addsuffix .o,$(PLAITS_LAB_TEST_SOURCES)))
SMOKE_SOURCES := $(PLAITS_LAB_SOURCES) $(PLAITS_LAB_VOICE_SOURCE) $(PLAITS_LAB_SMOKE_SOURCE)
SMOKE_OBJECTS := $(addprefix build/tests/,$(addsuffix .o,$(SMOKE_SOURCES)))
SMOKE_BIN := build/test_package-$(PLAITS_LAB_BUILD_KEY)

test: build/test_lab_voice $(SMOKE_BIN)
	build/test_lab_voice
	$(SMOKE_BIN)

build/test_lab_voice: $(VOICE_OBJECTS)
	$(CXX) $^ -o $@

$(SMOKE_BIN): $(SMOKE_OBJECTS)
	$(CXX) $^ -o $@

$(VOICE_OBJECTS): build/tests/%.o: %
	@mkdir -p $(@D)
	$(CXX) $(TEST_FLAGS) $(PLAITS_LAB_TEST_FLAGS) -c $< -o $@

$(SMOKE_OBJECTS): build/tests/%.o: %
	@mkdir -p $(@D)
	$(CXX) $(TEST_FLAGS) $(PLAITS_LAB_FLAGS) -c $< -o $@

-include $(VOICE_OBJECTS:.o=.d) $(SMOKE_OBJECTS:.o=.d)

# Optional test against the real Rack API; no Rack process or window is opened.
test-rack:
	$(CXX) -std=c++11 -O2 -DTEST -D_USE_MATH_DEFINES $(PLAITS_LAB_FLAGS) -I$(RACK_DIR)/include -I$(RACK_DIR)/dep/include tests/test_rack_state.cpp $(PLAITS_LAB_VOICE_SOURCE) $(PLAITS_LAB_SOURCES) -L$(RACK_DIR) -lRack -o build/test_rack_state
	PATH="$(RACK_DIR):$$PATH" LD_LIBRARY_PATH="$(RACK_DIR):$$LD_LIBRARY_PATH" DYLD_LIBRARY_PATH="$(RACK_DIR):$$DYLD_LIBRARY_PATH" build/test_rack_state

.PHONY: test test-rack clean
