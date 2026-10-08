# Discord Messenger for IRIX (and other Unix-like systems).
#
#   make [FRONTEND=motif|imgui|cli] [CXX=...] [PREFIX_DEPS=...]
#
# FRONTEND picks the user interface: motif (the X11/Motif client, for IRIX
# and other historic Unix), imgui (Dear ImGui on GLFW and OpenGL 3, for
# Linux and macOS) or cli (a text test client that exercises the core).
# PREFIX_DEPS is where OpenSSL and libwebp are installed (include/ and lib/
# below it); GLFW_PREFIX where GLFW is, when not there.

FRONTEND    ?= motif
DEBUG       ?= no
DISABLE_WEBP ?= 0
STATIC_DEPS ?= 0
PREFIX_DEPS ?= /usr/pkg
CXX         ?= c++
CC          ?= cc
EXTRA_CXXFLAGS ?=
EXTRA_LDFLAGS  ?=
X_CFLAGS    ?=
X_LIBS      ?= -lXm -lXt -lXext -lX11
FT_CFLAGS   ?= -I$(PREFIX_DEPS)/include/freetype2
FT_LIBS     ?= -lfreetype
GLFW_PREFIX ?= $(PREFIX_DEPS)
# (a static GLFW on macOS: $(GLFW_PREFIX)/lib/libglfw3.a and the frameworks
# Cocoa, IOKit, CoreFoundation and QuartzCore)
GLFW_LIBS   ?= -L$(GLFW_PREFIX)/lib -lglfw
UNAME       := $(shell uname -s)

BUILD_DIR ?= build-unix/$(FRONTEND)
TARGET    ?= bin/dm-$(FRONTEND)

DEFINES = \
	-DASIO_STANDALONE             \
	-DASIO_HAS_THREADS            \
	-DASIO_SEPARATE_COMPILATION   \
	-DDISCORD_MESSENGER           \
	-DCPPHTTPLIB_OPENSSL_SUPPORT  \
	-DCPPHTTPLIB_NO_EXCEPTIONS

INC_DIRS = -Isrc -Isrc/core -Ideps -Ideps/asio -Ideps/websocketpp \
	-I$(PREFIX_DEPS)/include

ifeq ($(DEBUG),yes)
OPT = -D_DEBUG -g -O0
else
OPT = -DNDEBUG -O2
endif

CXXFLAGS = $(INC_DIRS) $(DEFINES) -std=c++11 -pthread $(OPT) $(EXTRA_CXXFLAGS)
LDFLAGS  = -L$(PREFIX_DEPS)/lib -pthread $(EXTRA_LDFLAGS)
# STATIC_DEPS=1 links OpenSSL and libwebp from their static archives, so the
# program needs nothing but the system's own libraries.
ifeq ($(STATIC_DEPS),1)
LIBS     = $(PREFIX_DEPS)/lib/libssl.a $(PREFIX_DEPS)/lib/libcrypto.a
else
LIBS     = -lssl -lcrypto
endif

ifneq ($(DISABLE_WEBP),1)
ifeq ($(STATIC_DEPS),1)
LIBS += $(PREFIX_DEPS)/lib/libwebp.a $(PREFIX_DEPS)/lib/libsharpyuv.a
else
LIBS += -lwebp
endif
else
DEFINES += -DDISABLE_WEBP
endif

# FreeType sets the text (shared/Fonts) for every front end
CXXFLAGS += $(FT_CFLAGS)
LIBS += $(FT_LIBS)

ifeq ($(FRONTEND),motif)
CXXFLAGS += $(X_CFLAGS)
LIBS += $(X_LIBS)
endif

# Dear ImGui (deps/imgui, with its GLFW and OpenGL 3 back ends)
IMGUI_FILES :=
ifeq ($(FRONTEND),imgui)
CXXFLAGS += -Ideps/imgui -Ideps/imgui/backends -I$(GLFW_PREFIX)/include -DIMGUI_USE_WCHAR32
IMGUI_FILES := deps/imgui/imgui.cpp deps/imgui/imgui_draw.cpp deps/imgui/imgui_tables.cpp \
	deps/imgui/imgui_widgets.cpp deps/imgui/backends/imgui_impl_glfw.cpp \
	deps/imgui/backends/imgui_impl_opengl3.cpp
LIBS += $(GLFW_LIBS)
ifeq ($(UNAME),Darwin)
LIBS += -framework OpenGL
else
LIBS += -lGL -ldl
endif
endif

CXXFILES := \
	$(shell find src/core src/posix src/shared src/$(FRONTEND) -type f -name '*.cpp') \
	$(IMGUI_FILES) \
	deps/asio/src/asio.cpp \
	deps/asio/src/asio_ssl.cpp \
	deps/md5/MD5.cpp

CFILES := deps/qrcodegen/qrcodegen.c

OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CXXFILES)) $(patsubst %.c,$(BUILD_DIR)/%.o,$(CFILES))
DEP := $(OBJ:.o=.d)

.PHONY: all clean
all: $(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEP)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo ">> $<"
	@$(CXX) $(CXXFLAGS) -MMD -MF $(BUILD_DIR)/$*.d -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo ">> $<"
	@$(CC) -std=c99 $(OPT) $(EXTRA_CXXFLAGS:-std=%=) -MMD -MF $(BUILD_DIR)/$*.d -c $< -o $@

$(TARGET): $(OBJ) Makefile
	@mkdir -p bin
	@echo ">> linking $@"
	@$(CXX) -o $@ $(OBJ) $(LDFLAGS) $(LIBS)
