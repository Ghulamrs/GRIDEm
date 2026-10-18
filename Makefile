# =============================================================================
#  GRIDEm - GRIDE native on macOS
#
#    make            the library and the app (macOS)
#    make lib        build/libGRIDEm.a - the C++14 layers, no AppKit (any POSIX box)
#    make check      compiles every C++ source alone, as the contract asks:
#                    -std=c++14 -pedantic -Werror
#    make app        build/GRIDEm.app (macOS), with build/toolchain's tools and runtimes
#                    (Tools/build-toolchain.sh) copied into it
#    make run        builds and opens the app (macOS)
#    make test       the XCTest bundle, run with xcodebuild (macOS)
#    make tools      the console tools in Tools/ (GRIDEm-build, GRIDEm-toolcheck)
#    make xcodeproj  GRIDEm.xcodeproj from the lists below (python3 Tools/make-xcodeproj.py)
#    make clean
# =============================================================================

UNAME   := $(shell uname -s)
BUILD   := build
CXX     ?= c++
STD     := -std=c++14 -pedantic -Wall -Wextra -Werror
CXXFLAGS := $(STD) -O2 -g -ISources
OBJCFLAGS := $(STD) -O2 -g -ISources -fobjc-arc -fmodules

# ---- the C++ layers (kept in the order of the reference, section 1) ---------
CORE_SRC     := $(wildcard Sources/Core/*.cpp)
PLATFORM_SRC := $(wildcard Sources/Platform/*.cpp)
SETTINGS_SRC := $(wildcard Sources/Settings/*.cpp)
MODEL_SRC    := $(wildcard Sources/Model/*.cpp)
SERVICES_SRC := $(wildcard Sources/Services/*.cpp)
LIB_SRC      := $(CORE_SRC) $(PLATFORM_SRC) $(SETTINGS_SRC) $(MODEL_SRC) $(SERVICES_SRC)
LIB_OBJ      := $(patsubst Sources/%.cpp,$(BUILD)/obj/%.o,$(LIB_SRC))

# ---- Objective-C++ (macOS only) ---------------------------------------------
PLATFORM_MM  := $(wildcard Sources/Platform/*.mm)
UI_MM        := $(wildcard Sources/UI/*.mm) $(wildcard Sources/UI/*/*.mm)
APP_OBJ      := $(patsubst Sources/%.mm,$(BUILD)/obj/%.o,$(PLATFORM_MM) $(UI_MM))

TOOLS        := $(BUILD)/GRIDEm-build $(BUILD)/GRIDEm-toolcheck

ifeq ($(UNAME),Darwin)
CXXFLAGS  += -mmacosx-version-min=13.0
LDMIN     := -mmacosx-version-min=13.0
OBJCFLAGS += -mmacosx-version-min=13.0
LIBS_UTIL :=
FRAMEWORKS := -framework AppKit -framework CoreServices -framework WebKit -framework UniformTypeIdentifiers
all: lib app
else
LIBS_UTIL := -lpthread
all: lib
endif

lib: $(BUILD)/libGRIDEm.a

$(BUILD)/libGRIDEm.a: $(LIB_OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(BUILD)/obj/%.o: Sources/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/%.o: Sources/%.mm
	@mkdir -p $(dir $@)
	$(CXX) $(OBJCFLAGS) -MMD -MP -c $< -o $@

-include $(LIB_OBJ:.o=.d) $(APP_OBJ:.o=.d)

check:
	@for f in $(LIB_SRC); do \
	  $(CXX) $(STD) -fsyntax-only -ISources $$f || exit 1; done
	@echo "check: every C++ source compiles under -std=c++14 -pedantic -Werror"

tools: $(TOOLS)

$(BUILD)/GRIDEm-%: Tools/GRIDEm-%.cpp $(BUILD)/libGRIDEm.a
	$(CXX) $(CXXFLAGS) $< $(BUILD)/libGRIDEm.a $(LIBS_UTIL) -o $@

# ---- the app bundle (macOS) -------------------------------------------------
APP      := $(BUILD)/GRIDEm.app
TOOLCHAIN_BIN ?= $(BUILD)/toolchain/bin
TOOLCHAIN_LIB ?= $(BUILD)/toolchain/lib

app: $(APP)/Contents/MacOS/GRIDEm

$(APP)/Contents/MacOS/GRIDEm: $(APP_OBJ) $(BUILD)/libGRIDEm.a Resources/Info.plist
	@mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources
	$(CXX) $(LDMIN) $(APP_OBJ) $(BUILD)/libGRIDEm.a $(FRAMEWORKS) -o $@
	cp Resources/Info.plist $(APP)/Contents/Info.plist
	cp Resources/toolchain.json $(APP)/Contents/Resources/toolchain.json
	@if [ -d Resources/lib ]; then cp -R Resources/lib $(APP)/Contents/Resources/; fi
	@if [ -d Resources/docs ]; then cp -R Resources/docs $(APP)/Contents/Resources/; fi
	@if [ -d Resources/samples ]; then cp -R Resources/samples $(APP)/Contents/Resources/; fi
	@if [ -f Resources/Credits.rtf ]; then cp Resources/Credits.rtf $(APP)/Contents/Resources/; fi
	@if [ -f Resources/GRIDEm.icns ]; then cp Resources/GRIDEm.icns $(APP)/Contents/Resources/; fi
	@if [ -d $(TOOLCHAIN_BIN) ]; then find $(TOOLCHAIN_BIN) -maxdepth 1 -type f -perm -u+x -exec cp {} $(APP)/Contents/MacOS/ \; ; fi
	@if [ -d $(TOOLCHAIN_LIB) ]; then mkdir -p $(APP)/Contents/Resources/lib && cp -R $(TOOLCHAIN_LIB)/. $(APP)/Contents/Resources/lib/; fi
	@# shalimar looks for its runtimes in lib/ beside it, then ../lib: Contents/lib
	@if [ ! -e $(APP)/Contents/lib ]; then ln -s Resources/lib $(APP)/Contents/lib; fi
	codesign --force --sign - $(APP) 2>/dev/null || true

run: app
	open $(APP)

xcodeproj:
	python3 Tools/make-xcodeproj.py

test: xcodeproj
	xcodebuild -project GRIDEm.xcodeproj -scheme GRIDEmTests test

clean:
	rm -rf $(BUILD)

.PHONY: all lib check tools app run xcodeproj test clean
