# Shared native/Buildroot build. All headers are internal to this application.
.DEFAULT_GOAL := all

CXX ?= g++
BUILD_DIR ?= local-out
OUTPUT ?= $(BUILD_DIR)/puppy
CPPFLAGS ?=
CXXFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=

CATALOG_SOURCES := src/domain/model.cpp src/catalog/catalog.cpp src/support/shell.cpp
PLATFORM_SOURCES := src/platform/platform.cpp
PRESENTATION_SOURCES := src/input/input.cpp src/ui/ui.cpp

CATALOG_OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CATALOG_SOURCES))
PLATFORM_OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(PLATFORM_SOURCES))
PRESENTATION_OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(PRESENTATION_SOURCES))
PALETTE_OBJECT := $(BUILD_DIR)/src/palette/palette.o
MAIN_OBJECT := $(BUILD_DIR)/src/main.o

CATALOG_LIBRARY := $(BUILD_DIR)/libpuppy-catalog.a
PLATFORM_LIBRARY := $(BUILD_DIR)/libpuppy-platform.a
PRESENTATION_LIBRARY := $(BUILD_DIR)/libpuppy-presentation.a

PALETTE_LIBRARY := $(BUILD_DIR)/libpuppy-palette.a

TEST_OBJECT := $(BUILD_DIR)/tests-build/regression.o
TEST_BINARY := $(BUILD_DIR)/tests/regression

.PHONY: all test
all: $(OUTPUT)

$(OUTPUT): $(MAIN_OBJECT) $(PLATFORM_LIBRARY) $(PRESENTATION_LIBRARY) $(PALETTE_LIBRARY) $(CATALOG_LIBRARY)
	@mkdir -p "$(@D)"
	$(CXX) $(LDFLAGS) -o "$@" $^ $(LDLIBS) -pthread -lSDL2 -lSDL2_image -lSDL2_ttf

$(CATALOG_LIBRARY): $(CATALOG_OBJECTS)
$(PLATFORM_LIBRARY): $(PLATFORM_OBJECTS)
$(PRESENTATION_LIBRARY): $(PRESENTATION_OBJECTS)
$(PALETTE_LIBRARY): $(PALETTE_OBJECT)

$(CATALOG_LIBRARY) $(PLATFORM_LIBRARY) $(PRESENTATION_LIBRARY) $(PALETTE_LIBRARY): Makefile
	rm -f "$@"
	$(AR) rcs "$@" $(filter %.o,$^)

$(BUILD_DIR)/%.o: %.cpp Makefile
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) -Isrc $(CXXFLAGS) -std=c++17 -Wall -Wextra -MMD -MP -c "$<" -o "$@"

# Separate objects keep assertions enabled even with release compiler flags.
$(BUILD_DIR)/tests-build/%.o: tests/%.cpp Makefile
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) -Isrc $(CXXFLAGS) -std=c++17 -Wall -Wextra -Werror -UNDEBUG -MMD -MP -c "$<" -o "$@"

$(TEST_BINARY): $(TEST_OBJECT) $(PRESENTATION_LIBRARY) $(PALETTE_LIBRARY) $(CATALOG_LIBRARY)
	@mkdir -p "$(@D)"
	$(CXX) $(LDFLAGS) -o "$@" $^ $(LDLIBS) -lSDL2

test: $(TEST_BINARY)
	@set -eu; fixture_dir=$$(mktemp -d); \
	trap 'rm -rf "$$fixture_dir"' EXIT HUP INT TERM; \
	"$(abspath $(TEST_BINARY))" "$$fixture_dir"

-include $(MAIN_OBJECT:.o=.d) $(CATALOG_OBJECTS:.o=.d) $(PLATFORM_OBJECTS:.o=.d) \
         $(PRESENTATION_OBJECTS:.o=.d) $(PALETTE_OBJECT:.o=.d) $(TEST_OBJECT:.o=.d)
