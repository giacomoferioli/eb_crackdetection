CXX ?= g++

TARGET := build/image_processor
SRC_DIR := src
OBJ_DIR := objs

SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS := -Iinclude $(shell pkg-config --cflags sdl2 opencv4)
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDFLAGS := $(shell pkg-config --libs sdl2 opencv4)

# OpenEB is optional so the project remains buildable before the camera SDK is
# installed. It is enabled automatically when its camera header is found; use
# OPENEB=1 to force it. The paths can be overridden for a custom installation.
OPENEB ?= auto
OPENEB_PREFIX ?= /usr/local
OPENEB_CXXFLAGS ?= -I$(OPENEB_PREFIX)/include
OPENEB_LDLIBS ?= -L$(OPENEB_PREFIX)/lib -lmetavision_sdk_stream \
	-lmetavision_sdk_core -lmetavision_sdk_base -lmetavision_hal
OPENEB_HEADER := $(firstword $(wildcard \
	$(OPENEB_PREFIX)/include/metavision/sdk/stream/camera.h \
	/usr/include/metavision/sdk/stream/camera.h))

ifeq ($(OPENEB),auto)
ifneq ($(OPENEB_HEADER),)
OPENEB := 1
else
OPENEB := 0
endif
endif

ifeq ($(OPENEB),1)
CPPFLAGS += -DHAVE_OPENEB=1 $(OPENEB_CXXFLAGS)
LDFLAGS += $(OPENEB_LDLIBS)
endif

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJECTS) | build
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

build $(OBJ_DIR):
	mkdir -p $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)

-include $(DEPS)
