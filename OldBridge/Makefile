# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.
LDFLAGS +=

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk

# Generate .vcvm presets from src/presets/<slug>/<name>.json into presets/.
# Uses helpers/create_presets.py (one call per module that has a src/presets dir).
presets:
	@for d in $(wildcard src/presets/*/); do \
		slug=$$(basename $$d); \
		python3 helpers/create_presets.py $$slug; \
	done
	@echo "Presets generated from src/presets"

# Presets are part of DISTRIBUTABLES, so regenerate them for the dist package
dist: presets

.PHONY: presets
