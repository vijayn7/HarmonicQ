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
PISTACHE_PREFIX ?= $(shell brew --prefix pistache 2>/dev/null || echo "")

# Essentia install used by the project (override if needed)
ESSENTIA_PREFIX ?= /Users/$(USER)/.local/share/harmonicq/essentia-install
ESSENTIA_PKGCONFIG_DIR ?= $(ESSENTIA_PREFIX)/lib/pkgconfig

export PKG_CONFIG_PATH := $(ESSENTIA_PKGCONFIG_DIR):$(PKG_CONFIG_PATH)

ESSENTIA_CFLAGS := $(shell pkg-config --cflags --silence-errors essentia 2>/dev/null)
ESSENTIA_LIBS   := $(shell pkg-config --libs --silence-errors essentia 2>/dev/null)

# Build include flags
INCLUDES := $(ESSENTIA_CFLAGS)
ifneq ($(NLOHMANN_PREFIX),)
INCLUDES += -I$(NLOHMANN_PREFIX)/include
endif
ifneq ($(PISTACHE_PREFIX),)
INCLUDES += -I$(PISTACHE_PREFIX)/include
LDFLAGS  += -L$(PISTACHE_PREFIX)/lib -lpistache -Wl,-rpath,$(PISTACHE_PREFIX)/lib
endif

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
	@printf "\n$(CLR_BLUE)================================ Run Output ================================$(CLR_RESET)\n"
	@printf "$(CLR_YELLOW)Running %s$(CLR_RESET)\n" "$(TARGET)"
	@./$(TARGET) || { printf "$(CLR_RED)Program exited with non-zero status$(CLR_RESET)\n"; exit 1; }

clean:
	@rm -rf $(BUILD_DIR)
	@printf "$(CLR_YELLOW)Cleaned build artifacts$(CLR_RESET)\n"