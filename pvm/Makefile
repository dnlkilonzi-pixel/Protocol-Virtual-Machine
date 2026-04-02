# =============================================================================
# Makefile — Protocol Virtual Machine (PVM)
#
# Automatically detects the host OS and selects:
#   • The correct Platform Abstraction Layer implementation
#   • The correct shared-library extension for protocol modules
#
# Targets:
#   all           Build everything (modules + PVM binary)
#   modules       Build only the protocol module shared libraries
#   pvm_demo      Build only the PVM binary (assumes modules are built)
#   test          Build everything and run the demo
#   clean         Remove all build artefacts
# =============================================================================

# ---------------------------------------------------------------------------
# OS detection
# ---------------------------------------------------------------------------
UNAME_S := $(shell uname -s 2>/dev/null || echo "Windows_NT")

ifeq ($(UNAME_S),Linux)
    PLATFORM_SRC  := platform/linux/platform_linux.c
    PLATFORM_DEF  := -DPLATFORM_LINUX
    MODULE_EXT    := .so
    LDFLAGS_EXTRA := -ldl -lpthread
    MODULE_LDFLAGS := -shared -fPIC
endif

ifeq ($(UNAME_S),Darwin)
    PLATFORM_SRC  := platform/macos/platform_macos.c
    PLATFORM_DEF  := -DPLATFORM_MACOS
    MODULE_EXT    := .dylib
    LDFLAGS_EXTRA := -ldl
    MODULE_LDFLAGS := -shared -fPIC
endif

ifeq ($(OS),Windows_NT)
    PLATFORM_SRC  := platform/windows/platform_windows.c
    PLATFORM_DEF  := -DPLATFORM_WINDOWS
    MODULE_EXT    := .dll
    LDFLAGS_EXTRA := -lws2_32
    MODULE_LDFLAGS := -shared
endif

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------
CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -Iinclude $(PLATFORM_DEF) \
           -DPVM_MODULE_EXT=\"$(MODULE_EXT)\"
LDFLAGS := $(LDFLAGS_EXTRA)

# ---------------------------------------------------------------------------
# Source files for the main PVM binary
# ---------------------------------------------------------------------------
PVM_SRCS := \
    main.c                              \
    core/pvm.c                          \
    net/packet.c                        \
    net/dispatcher.c                    \
    platform/module_loader.c            \
    $(PLATFORM_SRC)

PVM_TARGET := pvm_demo

# ---------------------------------------------------------------------------
# Protocol module shared libraries
# ---------------------------------------------------------------------------
UDP_SRC    := modules/udp/udp_module.c
UDP_LIB    := modules/udp/udp$(MODULE_EXT)

VESPER_SRC := modules/vesper_lite/vesper_lite_module.c
VESPER_LIB := modules/vesper_lite/vesper_lite$(MODULE_EXT)

ALL_MODULES := $(UDP_LIB) $(VESPER_LIB)

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------
.PHONY: all modules pvm_demo test clean

all: modules $(PVM_TARGET)

# Build all protocol modules
modules: $(ALL_MODULES)

# UDP module shared library
$(UDP_LIB): $(UDP_SRC)
	$(CC) $(CFLAGS) $(MODULE_LDFLAGS) -o $@ $<
	@echo "  Built $(UDP_LIB)"

# VESPER-LITE module shared library
$(VESPER_LIB): $(VESPER_SRC)
	$(CC) $(CFLAGS) $(MODULE_LDFLAGS) -o $@ $<
	@echo "  Built $(VESPER_LIB)"

# PVM binary
$(PVM_TARGET): $(PVM_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "  Built $(PVM_TARGET)"

# Build and run the demo
test: all
	@echo ""
	@echo "Running PVM demo..."
	@echo ""
	./$(PVM_TARGET)

clean:
	rm -f $(PVM_TARGET) $(ALL_MODULES)
	@echo "  Cleaned."
