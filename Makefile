# Makefile for distingNT C++ Plugin (Hardware + Testing)

# Plugin name
PLUGIN_NAME = SwMx

# Source files
SOURCES = SwitchingMixer.cpp

# Detect platform
UNAME_S := $(shell uname -s)

# Target selection (hardware or test)
TARGET ?= hardware

# ============================================================================
# HARDWARE BUILD (ARM Cortex-M7 for distingNT)
# ============================================================================
ifeq ($(TARGET),hardware)
    CXX = arm-none-eabi-g++
    CFLAGS = -std=c++17 \
             -mcpu=cortex-m7 \
             -mfpu=fpv5-d16 \
             -mfloat-abi=hard \
             -mthumb \
             -Os \
             -Wall \
             -fPIC \
             -fno-rtti \
             -fno-exceptions
    INCLUDES = -I. -I$(DISTING_NT_API_PATH)/include
    LDFLAGS = -Wl,--relocatable -nostdlib
    OUTPUT_DIR = plugins
    BUILD_DIR = build_arm
    OUTPUT = $(OUTPUT_DIR)/$(PLUGIN_NAME).o
    OBJECTS = $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(SOURCES))
    CHECK_CMD = arm-none-eabi-nm $(OUTPUT) | grep ' U '
    SIZE_CMD = arm-none-eabi-size $(OUTPUT)

# ============================================================================
# DESKTOP BUILD (Native for nt_emu VCV Rack testing)
# ============================================================================
else ifeq ($(TARGET),test)
    # macOS
    ifeq ($(UNAME_S),Darwin)
        CXX = clang++
        CFLAGS = -std=c++17 -fPIC -Os -Wall -fno-rtti -fno-exceptions
        LDFLAGS = -dynamiclib -undefined dynamic_lookup
        EXT = dylib
    endif

    # Linux
    ifeq ($(UNAME_S),Linux)
        CXX = g++
        CFLAGS = -std=c++17 -fPIC -Os -Wall -fno-rtti -fno-exceptions
        LDFLAGS = -shared
        EXT = so
    endif

    INCLUDES = -I. -I$(DISTING_NT_API_PATH)/include
    OUTPUT_DIR = plugins
    OUTPUT = $(OUTPUT_DIR)/$(PLUGIN_NAME).$(EXT)
    CHECK_CMD = nm $(OUTPUT) | grep ' U ' || echo "No undefined symbols"
    SIZE_CMD = ls -lh $(OUTPUT)
endif

# Path to distingNT API
DISTING_NT_API_PATH ?= /Users/nealsanche/github/distingNT_API

# ============================================================================
# BUILD RULES
# ============================================================================

# Default target
all: $(OUTPUT)

# Hardware build with object files
ifeq ($(TARGET),hardware)
$(OUTPUT): $(OBJECTS)
	@mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CFLAGS) $(LDFLAGS) -o $@ $^
	@echo "Built hardware plugin: $@"

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Test build (direct linking)
else ifeq ($(TARGET),test)
$(OUTPUT): $(SOURCES)
	@mkdir -p $(OUTPUT_DIR)
	$(CXX) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $(SOURCES)
	@echo "Built test plugin: $@"
endif

# Build for hardware
hardware:
	@$(MAKE) TARGET=hardware

# Build for testing
test:
	@$(MAKE) TARGET=test

# Build both targets
both: hardware test

# Check for undefined symbols
check: $(OUTPUT)
	@echo "Checking symbols in $(OUTPUT)..."
	@$(CHECK_CMD) || true

# Show plugin size
size: $(OUTPUT)
	@echo "Size of $(OUTPUT):"
	@$(SIZE_CMD)

# Clean all build artifacts
clean:
	rm -rf $(BUILD_DIR) build_arm $(OUTPUT_DIR)
	@echo "Cleaned build and output directories"

.PHONY: all hardware test both check size clean
