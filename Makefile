# Discord Messenger: IRIX (Motif); macOS, Linux and Windows (Dear ImGui).
#
#   make [FRONTEND=motif|imgui|cli] [CXX=...] [PREFIX_DEPS=...]
#
# FRONTEND picks the user interface: motif (the X11/Motif client, for IRIX
# and other historic Unix), imgui (Dear ImGui on GLFW and OpenGL 3, for
# Linux, macOS and Windows) or cli (a text test client that exercises the core).
# IMGUI_PLATFORM=x11 builds the imgui one on Xlib, GLX 1.2 and OpenGL 1.1
# instead (IRIX), with X_CFLAGS and X11_LIBS for X.
# PREFIX_DEPS is where OpenSSL and libwebp are installed (include/ and lib/
# below it); GLFW_PREFIX where GLFW is, when not there.
#
# TARGET_OS=windows cross-builds for Windows with a clang that targets the
# MSVC ABI (CXX=x86_64-windows-clang++ or aarch64-windows-clang++), against
# static libraries in PREFIX_DEPS as their CMake builds name them.

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
IMGUI_PLATFORM ?= glfw
X11_LIBS    ?= -lGL -lX11
TARGET_OS   ?=
ifeq ($(TARGET_OS),windows)
FT_LIBS     ?= $(PREFIX_DEPS)/lib/freetype.lib $(PREFIX_DEPS)/lib/libpng16_static.lib $(PREFIX_DEPS)/lib/zlib.lib
GLFW_LIBS   ?= $(PREFIX_DEPS)/lib/glfw3.lib
endif
FT_CFLAGS   ?= -I$(PREFIX_DEPS)/include/freetype2
# FreeType, and libpng (FreeType's colour emoji, and Discord's default
# avatars)
FT_LIBS     ?= -lfreetype -lpng16
GLFW_PREFIX ?= $(PREFIX_DEPS)
# (a static GLFW on macOS: $(GLFW_PREFIX)/lib/libglfw3.a and the frameworks
# Cocoa, IOKit, CoreFoundation and QuartzCore)
GLFW_LIBS   ?= -L$(GLFW_PREFIX)/lib -lglfw
UNAME       := $(shell uname -s)

BUILD_DIR ?= build-unix/$(FRONTEND)
ifeq ($(TARGET_OS),windows)
TARGET    ?= bin/dm-$(FRONTEND).exe
else
TARGET    ?= bin/dm-$(FRONTEND)
endif

DEFINES = \
	-DDISCORD_MESSENGER

INC_DIRS = -Isrc -Isrc/core -Ideps \
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

ifeq ($(TARGET_OS),windows)
LIBS     = $(PREFIX_DEPS)/lib/libssl.a $(PREFIX_DEPS)/lib/libcrypto.a
endif

ifneq ($(DISABLE_WEBP),1)
ifeq ($(TARGET_OS),windows)
LIBS += $(PREFIX_DEPS)/lib/webp.lib $(PREFIX_DEPS)/lib/sharpyuv.lib
else ifeq ($(STATIC_DEPS),1)
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

# Dear ImGui (deps/imgui, with its GLFW and OpenGL 3 back ends, or its
# OpenGL 2 one under our X11 platform layer)
IMGUI_FILES :=
ifeq ($(FRONTEND),imgui)
CXXFLAGS += -Ideps/imgui -Ideps/imgui/backends -DIMGUI_USE_WCHAR32 '-DIMGUI_USER_CONFIG="imgui/imconfig_dm.h"'
IMGUI_FILES := deps/imgui/imgui.cpp deps/imgui/imgui_draw.cpp deps/imgui/imgui_tables.cpp \
	deps/imgui/imgui_widgets.cpp
ifeq ($(IMGUI_PLATFORM),x11)
DEFINES += -DDM_IMGUI_X11
CXXFLAGS += $(X_CFLAGS)
IMGUI_FILES += deps/imgui/backends/imgui_impl_opengl2.cpp
LIBS += $(X11_LIBS)
else
CXXFLAGS += -I$(GLFW_PREFIX)/include
IMGUI_FILES += deps/imgui/backends/imgui_impl_glfw.cpp deps/imgui/backends/imgui_impl_opengl3.cpp
LIBS += $(GLFW_LIBS)
endif
ifeq ($(IMGUI_PLATFORM),x11)
else ifeq ($(TARGET_OS),windows)
LIBS += -lopengl32
else ifeq ($(UNAME),Darwin)
# the browser view for logging in (Objective-C++)
MMFILES := $(shell find src/$(FRONTEND) -type f -name '*.mm')
LIBS += -framework OpenGL -framework Cocoa -framework WebKit
else
LIBS += -lGL -ldl
endif
endif

# Windows: the POSIX calls the code makes come from src/compat/win
COMPAT_FILES :=
RES_FILES :=
ifeq ($(TARGET_OS),windows)
DEFINES += -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
	-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS -D_CRT_DECLARE_NONSTDC_NAMES=1
INC_DIRS += -Isrc/compat/win
# (Microsoft's C++ library needs C++14 at least)
CXXFLAGS += -std=c++17 -include src/compat/win/dm_win.h
COMPAT_FILES := $(wildcard src/compat/win/*.cpp)
# the manifest, icon and version (windows/dm.rc)
RC        ?= llvm-rc
RES_FILES := $(BUILD_DIR)/dm.res
ifeq ($(FRONTEND),imgui)
# a windowed program (no console), main() as everywhere; WebView2 for the
# login on discord.com's page (its SDK's headers and static loader in
# PREFIX_DEPS)
LDFLAGS += -Wl,/subsystem:windows -Wl,/entry:mainCRTStartup
LIBS += $(PREFIX_DEPS)/lib/WebView2LoaderStatic.lib -lshlwapi -lversion -loleaut32 -ldwmapi
endif
LIBS += -lws2_32 -lmswsock -lcrypt32 -luser32 -lgdi32 -lshell32 -ladvapi32 -lwinmm -lole32
endif

# macOS checks the servers' certificates itself (src/posix/SystemTrust.cpp);
# so does Windows, through crypt32
ifeq ($(UNAME),Darwin)
LIBS += -framework Security -framework CoreFoundation
endif

CXXFILES := \
	$(shell find src/core src/posix src/shared src/$(FRONTEND) -type f -name '*.cpp') \
	$(IMGUI_FILES) $(COMPAT_FILES)

CFILES := deps/qrcodegen/qrcodegen.c

MMFILES ?=
OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CXXFILES)) $(patsubst %.c,$(BUILD_DIR)/%.o,$(CFILES)) \
	$(patsubst %.mm,$(BUILD_DIR)/%.o,$(MMFILES)) $(RES_FILES)
DEP := $(filter %.d,$(OBJ:.o=.d))

.PHONY: all clean
all: $(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEP)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo ">> $<"
	@$(CXX) $(CXXFLAGS) -MMD -MP -MF $(BUILD_DIR)/$*.d -c $< -o $@

$(BUILD_DIR)/%.o: %.mm
	@mkdir -p $(dir $@)
	@echo ">> $<"
	@$(CXX) $(CXXFLAGS) -fobjc-arc -MMD -MP -MF $(BUILD_DIR)/$*.d -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo ">> $<"
	@$(CC) -std=c99 $(OPT) $(EXTRA_CXXFLAGS:-std=%=) -MMD -MP -MF $(BUILD_DIR)/$*.d -c $< -o $@

# the version: VERSION (major.minor) and DM_BUILD (the build number, 0 for
# a developer build), filled into the resource templates
DM_VERSION := $(shell cat VERSION)
DM_BUILD   ?= 0
DM_VERSION_FULL := $(DM_VERSION).$(DM_BUILD)
comma := ,
$(BUILD_DIR)/rc/dm.rc $(BUILD_DIR)/rc/dm.manifest: windows/dm.rc windows/dm.manifest VERSION
	@mkdir -p $(BUILD_DIR)/rc
	@sed -e 's|@VERSION_FULL@|$(DM_VERSION_FULL)|g' windows/dm.manifest > $(BUILD_DIR)/rc/dm.manifest
	@sed -e 's|@VERSION_FULL@|$(DM_VERSION_FULL)|g' -e 's|@VERSION_COMMAS@|$(subst .,$(comma),$(DM_VERSION_FULL)),0|g' \
		-e 's|@MANIFEST@|dm.manifest|' -e 's|@ICON@|$(CURDIR)/irix/icon_discord.ico|' windows/dm.rc > $(BUILD_DIR)/rc/dm.rc

$(BUILD_DIR)/dm.res: $(BUILD_DIR)/rc/dm.rc
	@mkdir -p $(dir $@)
	@echo ">> windows/dm.rc"
	@$(RC) -FO $@ $<

$(TARGET): $(OBJ) Makefile
	@mkdir -p $(dir $@)
	@echo ">> linking $@"
	@$(CXX) -o $@ $(OBJ) $(LDFLAGS) $(LIBS)
