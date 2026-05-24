BUILD_DIR ?= build
TARGET := $(BUILD_DIR)/harmonicq_analyze
SRCDIR := src
SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

# Terminal colors for status messages
CLR_RESET := \033[0m
CLR_GREEN := \033[32m
CLR_YELLOW := \033[33m
CLR_BLUE := \033[34m
CLR_RED := \033[31m

# Optional package prefixes (brew) — empty if not installed
NLOHMANN_PREFIX ?= $(shell brew --prefix nlohmann-json 2>/dev/null || echo "")

# Prefer the vendored Pistache build so the binary matches the headers in this repo.
PISTACHE_ROOT ?= $(abspath dependencies/pistache)
PISTACHE_INCLUDE_DIR ?= $(PISTACHE_ROOT)/include
PISTACHE_LIB_DIR ?= $(PISTACHE_ROOT)/build/src

# Essentia install used by the project (override if needed)
ESSENTIA_PREFIX ?= /Users/$(USER)/.local/share/harmonicq/essentia-install
ESSENTIA_PKGCONFIG_DIR ?= $(ESSENTIA_PREFIX)/lib/pkgconfig

export PKG_CONFIG_PATH := $(ESSENTIA_PKGCONFIG_DIR):$(PKG_CONFIG_PATH)

ESSENTIA_CFLAGS := $(shell pkg-config --cflags --silence-errors essentia 2>/dev/null)
ESSENTIA_LIBS   := $(shell pkg-config --libs --silence-errors essentia 2>/dev/null)

# Build include flags
INCLUDES := $(ESSENTIA_CFLAGS) -I$(PISTACHE_INCLUDE_DIR)
ifneq ($(NLOHMANN_PREFIX),)
INCLUDES += -I$(NLOHMANN_PREFIX)/include
endif

LDFLAGS += -L$(PISTACHE_LIB_DIR) -lpistache -Wl,-rpath,$(PISTACHE_LIB_DIR)

LDFLAGS += $(ESSENTIA_LIBS) -Wl,-rpath,$(ESSENTIA_PREFIX)/lib

CXXFLAGS += $(INCLUDES)

.PHONY: all build run clean

all: build

$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	@if [ -f "$(ESSENTIA_PREFIX)/lib/libessentia.dylib" ]; then \
		install_name_tool -id "$(ESSENTIA_PREFIX)/lib/libessentia.dylib" "$(ESSENTIA_PREFIX)/lib/libessentia.dylib" >/dev/null 2>&1 || true; \
	fi
	@printf "$(CLR_BLUE)Linking %s...$(CLR_RESET)\n" "$@"
	@$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS) || { printf "$(CLR_RED)Link failed for %s$(CLR_RESET)\n" "$@"; exit 1; }
	@printf "$(CLR_GREEN)Build succeeded: %s$(CLR_RESET)\n" "$@"

$(BUILD_DIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	@printf "$(CLR_BLUE)Compiling %s...$(CLR_RESET)\n" "$<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@ || { printf "$(CLR_RED)Failed compiling %s$(CLR_RESET)\n" "$<"; exit 1; }
	@printf "$(CLR_GREEN) OK$(CLR_RESET)\n"

build: $(TARGET)

run: build
	@# Robustly kill any process listening on port 9080, then wait for port to free.
	@sh -c 'set -euo pipefail; PORT=9080; PIDS=$$(lsof -ti tcp:$$PORT 2>/dev/null || true) || true; if [ -n "$$PIDS" ]; then printf "$(CLR_YELLOW)Stopping processes on port $$PORT: %s$(CLR_RESET)\n" "$$PIDS"; EXTRA_PIDS=$$(ps -o ppid= -p $$PIDS 2>/dev/null || true | tr "\n" " ") || true; if [ -n "$$EXTRA_PIDS" ]; then printf "$(CLR_YELLOW)Also stopping parent PIDs: %s$(CLR_RESET)\n" "$$EXTRA_PIDS"; kill -9 $$EXTRA_PIDS >/dev/null 2>&1 || true; fi; kill -9 $$PIDS >/dev/null 2>&1 || true; fi; WAIT=20; COUNT=0; STILL=""; while [ $$COUNT -lt $$WAIT ]; do sleep 0.1; STILL=$$(lsof -ti tcp:$$PORT 2>/dev/null || true) || true; if [ -z "$$STILL" ]; then break; fi; COUNT=$$((COUNT+1)); done; if [ -n "$$STILL" ]; then printf "$(CLR_RED)Port $$PORT still in use after kill attempts: %s$(CLR_RESET)\n" "$$STILL"; printf "$(CLR_RED)Detailed socket info:$(CLR_RESET)\n"; lsof -i tcp:$$PORT || true; printf "$(CLR_RED)Aborting run due to port conflict$(CLR_RESET)\n"; exit 1; fi'
	@printf "\n$(CLR_BLUE)================================ Run Output ================================$(CLR_RESET)\n"
	@printf "$(CLR_YELLOW)Running %s$(CLR_RESET)\n" "$(TARGET)"
	@./$(TARGET) || { printf "$(CLR_RED)Program exited with non-zero status$(CLR_RESET)\n"; exit 1; }

clean:
	@rm -rf $(BUILD_DIR)
	@printf "$(CLR_YELLOW)Cleaned build artifacts$(CLR_RESET)\n"