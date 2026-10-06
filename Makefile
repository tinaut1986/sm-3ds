.SUFFIXES:

PWD= $(dir $(abspath $(firstword $(MAKEFILE_LIST))))
.DEFAULT_GOAL := 3dsx

#---------------------------------------------------------------------------------
# Environment Setup
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
export DEVKITPRO := /opt/devkitpro
endif

ifeq ($(strip $(DEVKITARM)),)
export DEVKITARM := $(DEVKITPRO)/devkitARM
endif

include $(DEVKITARM)/3ds_rules

# ip address of 3ds for hblauncher/fbi target.
IP3DS := 172.20.10.2

# FTP server on the console (ftpd, FBI) for the `ftp` target.
FTP_HOST ?= $(IP3DS)
FTP_PORT ?= 5000

#---------------------------------------------------------------------------------
# Version, derived from git (same scheme as ../mzm)
#---------------------------------------------------------------------------------
# - HEAD exactly on a tag:        that tag, e.g. v0.1.0
# - on (or branched off) release/vX.Y.Z:
#                                 vX.Y.Z-dev.<commits since main>[.<commits since
#                                 the release branch>]+<hash>
# - anything else:                git describe
# Override with `make VERSION=...`.
GIT_BRANCH := $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null)
EXACT_TAG := $(shell git describe --tags --exact-match 2>/dev/null)
# Newest release/* branch (local or origin) that HEAD contains, so topic
# branches cut from a release branch inherit its version.
REL_MERGED := $(shell git for-each-ref --format='%(refname:short)' --merged HEAD refs/heads/'release/*' refs/remotes/'origin/release/*' 2>/dev/null | sort -V | tail -1)
REL_BASE := $(shell git rev-parse --verify -q refs/remotes/origin/main >/dev/null && echo refs/remotes/origin/main || echo main)

ifneq ($(strip $(EXACT_TAG)),)
  GIT_VERSION := $(EXACT_TAG)
else
ifeq ($(filter release/%,$(GIT_BRANCH)),)
  REL_NAME := $(subst origin/,,$(REL_MERGED))
else
  REL_NAME := $(GIT_BRANCH)
endif
ifneq ($(filter release/%,$(REL_NAME)),)
  REL_TARGET := $(shell git rev-parse --verify -q refs/heads/$(REL_NAME) >/dev/null && echo refs/heads/$(REL_NAME) || echo refs/remotes/origin/$(REL_NAME))
  REL_CNT := $(shell git rev-list --count $(REL_BASE)..$(REL_TARGET) 2>/dev/null || echo 1)
  DEV_CNT := $(shell git rev-list --count $(REL_TARGET)..HEAD 2>/dev/null || echo 0)
  REL_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)
  ifeq ($(DEV_CNT),0)
    GIT_VERSION := $(patsubst release/%,%,$(REL_NAME))-dev.$(REL_CNT)+$(REL_HASH)
  else
    GIT_VERSION := $(patsubst release/%,%,$(REL_NAME))-dev.$(REL_CNT).$(DEV_CNT)+$(REL_HASH)
  endif
else
  GIT_VERSION := $(shell git describe --tags --always 2>/dev/null || echo "0.0-dev")
endif
endif
VERSION ?= $(GIT_VERSION)

# Numeric major/minor/micro for the CIA header, from a leading [v]X.Y.Z.
VERSION_NUMS := $(shell echo '$(VERSION)' | sed -nE 's/^v?([0-9]+)\.([0-9]+)\.([0-9]+).*/\1 \2 \3/p')
ifeq ($(words $(VERSION_NUMS)),3)
  APP_VER_MAJOR := $(word 1,$(VERSION_NUMS))
  APP_VER_MINOR := $(word 2,$(VERSION_NUMS))
  APP_VER_MICRO := $(word 3,$(VERSION_NUMS))
else
  APP_VER_MAJOR := 0
  APP_VER_MINOR := 0
  APP_VER_MICRO := 0
endif

#---------------------------------------------------------------------------------
# Directory Setup
#---------------------------------------------------------------------------------
BUILD := build
OUTPUT := output
SOURCES := source
DATA := data
INCLUDES := $(SOURCES) include
ROMFS := romfs
RESOURCES := resources

#---------------------------------------------------------------------------------
# Resource Setup
#---------------------------------------------------------------------------------
APP_INFO := $(RESOURCES)/AppInfo
BANNER_AUDIO := $(RESOURCES)/audio/audio
BANNER_IMAGE := $(RESOURCES)/banner
ICON := $(RESOURCES)/icon.png
RSF := $(TOPDIR)/$(RESOURCES)/template.rsf

#---------------------------------------------------------------------------------
# Build Setup (code generation)
#---------------------------------------------------------------------------------
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp

COMMON_FLAGS := -Wall -Wno-strict-aliasing -Wno-unused-value -Wno-unused-const-variable -Wno-unused-but-set-variable \
	-O3 -Ofast -mword-relocations -fomit-frame-pointer \
	-ffast-math $(ARCH) $(INCLUDE) -D__3DS__ -D_3DS -mno-unaligned-access $(BUILD_FLAGS)
# 	-flto -fwhole-program \
#     -funroll-loops \
#     -finline-functions \
#     -fgcse-sm -fgcse-las \
#     -fipa-pta \
#     -ftree-vectorize \
#     -fno-math-errno \
#     -fno-trapping-math \
#     -ffinite-math-only \
# 	-ffunction-sections -fdata-sections \
# 	-falign-functions=32 \
# 	-falign-loops=32

ifeq ($(FULL_NATIVE),1)
	EXTRA_CFLAGS := -DFULL_NATIVE
else
	EXTRA_CFLAGS :=
endif

CFLAGS := $(COMMON_FLAGS) -std=gnu99 -DSYSTEM_VOLUME_MIXER_AVAILABLE=1 $(EXTRA_CFLAGS)
CXXFLAGS := $(COMMON_FLAGS) -std=gnu++17
# CXXFLAGS += -fno-rtti -fno-exceptions

ASFLAGS := $(ARCH)
LDFLAGS = -specs=3dsx.specs $(ARCH) -Wl,-Map,$(notdir $*.map) \
		  -Wl,--gc-sections -Wl,--as-needed

# libcurl + mbedtls: the console's own TLS (httpc, used by RetroAchievements) cannot talk to GitHub, the updater needs them
LIBS := -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm
LIBDIRS := $(PORTLIBS) $(CTRULIB) ./lib

# SM game sources
SM_DIR := sm
SM_SRCS := $(wildcard $(SM_DIR)/src/*.c) $(wildcard $(SM_DIR)/src/snes/*.c) $(SM_DIR)/third_party/gl_core/gl_core_3_1.c
# main.c and the OpenGL output are the PC frontend; the 3DS build has no SDL (key codes: third_party/sdl_keys)
SM_SRCS := $(filter-out $(SM_DIR)/src/main.c $(SM_DIR)/src/opengl.c, $(SM_SRCS))
SM_CFILES := $(notdir $(SM_SRCS))

# rcheevos, the RetroAchievements library (third_party/rcheevos/VERSION.txt). Its
# rc_compat.h picks the libctru mutex with -D_3DS.
RC_DIR := third_party/rcheevos
RC_SRCS := $(wildcard $(RC_DIR)/src/*.c) $(wildcard $(RC_DIR)/src/rapi/*.c) \
	$(wildcard $(RC_DIR)/src/rcheevos/*.c) $(RC_DIR)/src/rhash/md5.c
RC_CFILES := $(notdir $(RC_SRCS))

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# Version File
#---------------------------------------------------------------------------------

include resources/AppInfo

# Generated at parse time and rewritten only when its content changes, so a new
# commit rebuilds main.c (and the SMDH) but nothing else.
VERSION_H := $(BUILD)/version.h
VERSION_H_TEXT := \#pragma once\n\#define APP_TITLE "$(APP_TITLE)"\n\#define APP_AUTHOR "$(APP_AUTHOR)"\n\#define APP_VERSION "$(VERSION)"\n
$(shell mkdir -p $(BUILD) && printf '$(VERSION_H_TEXT)' > $(VERSION_H).tmp && \
	(cmp -s $(VERSION_H).tmp $(VERSION_H) && rm -f $(VERSION_H).tmp || mv -f $(VERSION_H).tmp $(VERSION_H)))

# Build options the frontend reads, written the same way: switching one rebuilds the
# files that include build_config.h, with no `make clean`.
#   DEBUG_TOOLS=1  Debug tab, teleport, item/map editing on the Status tab.
DEBUG_TOOLS ?= 0
BUILD_CONFIG_H := $(BUILD)/build_config.h
BUILD_CONFIG_H_TEXT := \#pragma once\n\#define DEBUG_TOOLS $(DEBUG_TOOLS)\n
$(shell printf '$(BUILD_CONFIG_H_TEXT)' > $(BUILD_CONFIG_H).tmp && \
	(cmp -s $(BUILD_CONFIG_H).tmp $(BUILD_CONFIG_H) && rm -f $(BUILD_CONFIG_H).tmp || mv -f $(BUILD_CONFIG_H).tmp $(BUILD_CONFIG_H)))

#---------------------------------------------------------------------------------
# Build Variable Setup
#---------------------------------------------------------------------------------
recurse = $(shell find $2 -type $1 -name '$3' 2> /dev/null)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(call recurse,f,$(dir),*.c))) $(SM_CFILES) $(RC_CFILES)
ALL_CPP := $(foreach dir,$(SOURCES),$(call recurse,f,$(dir),*.cpp))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(call recurse,f,$(dir),*.cpp)))

SFILES := $(foreach dir,$(SOURCES),$(notdir $(call recurse,f,$(dir),*.s)))
PICAFILES := $(foreach dir,$(SOURCES),$(notdir $(call recurse,f,$(dir),*.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
	$(PICAFILES:.v.pica=.shbin.o) \
	$(SHLISTFILES:.shlist=.shbin.o) \
	$(CPPFILES:.cpp=.o) \
	$(CFILES:.c=.o) \
	$(SFILES:.s=.o)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD) \
	-I$(CURDIR)/third_party/sdl_keys \
	-I$(CURDIR)/$(SM_DIR) -I$(CURDIR)/$(SM_DIR)/src \
	-I$(CURDIR)/$(RC_DIR)/include -I$(CURDIR)/$(RC_DIR)/src -I$(CURDIR)/$(RC_DIR)/src/rhash

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export DEPSDIR := $(CURDIR)/$(BUILD)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir) $(call recurse,d,$(CURDIR)/$(dir),*)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir) $(call recurse,d,$(CURDIR)/$(dir),*)) \
                $(CURDIR)/$(SM_DIR)/src $(CURDIR)/$(SM_DIR)/src/snes $(CURDIR)/$(SM_DIR)/third_party/gl_core \
                $(CURDIR)/$(RC_DIR)/src $(CURDIR)/$(RC_DIR)/src/rapi $(CURDIR)/$(RC_DIR)/src/rcheevos \
                $(CURDIR)/$(RC_DIR)/src/rhash

export TOPDIR := $(CURDIR)
OUTPUT_DIR := $(TOPDIR)/$(OUTPUT)
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
OUTPUT_FILE := $(OUTPUT_DIR)/$(subst $(SPACE),,$(APP_TITLE))

.PHONY: $(BUILD) clean all format print-version ftp test

#---------------------------------------------------------------------------------
# Initial Targets
#---------------------------------------------------------------------------------
all: $(BUILD) $(OUTPUT_DIR)
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

3dsx: $(BUILD) $(OUTPUT_DIR)
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $@

cia: $(BUILD) $(OUTPUT_DIR)
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $@

print-version:
	@echo $(VERSION)

# Host regression tests (tools/test/README.md). Needs the ROM: make test SM_ROM=/path/rom.sfc
# Add TEST_ARGS=--full for the teleport test too.
test:
	@tools/test/run.sh $(TEST_ARGS) "$(SM_ROM)"

# Build the CIA and upload it to the console's FTP server as
# cias/sm-3ds-<VERSION>.cia (install it from there with FBI).
#   make -j FULL_NATIVE=1 ftp FTP_HOST=192.168.1.50 [FTP_PORT=5000]
ftp: cia
	@echo "Uploading $(notdir $(OUTPUT_FILE)).cia as sm-3ds-$(VERSION).cia to FTP..."
	curl --ftp-create-dirs -T $(OUTPUT_FILE).cia "ftp://$(FTP_HOST):$(FTP_PORT)/cias/sm-3ds-$(VERSION).cia"

3ds: $(BUILD) $(OUTPUT_DIR)
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $@

elf: $(BUILD) $(OUTPUT_DIR)
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $@

azahar: $(BUILD) $(OUTPUT_DIR)
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $@

hblauncher: $(BUILD) $(OUTPUT_DIR)
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $@

fbi: $(BUILD) $(OUTPUT_DIR)
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $@

$(BUILD):
	@[ -d $@ ] || mkdir -p $@

$(OUTPUT_DIR):
	@[ -d $@ ] || mkdir -p $@

fmt:
	find . -regex '.*\.\(c\|cc\|cpp\|cxx\|h\|hh\|hpp\)' -exec clang-format -i {} +

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT) $(DEVEL_OBJECTS) $(PARSER_OUT)

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# Build Information Setup
#---------------------------------------------------------------------------------
DEPENDS := $(OFILES:.o=.d)

include $(TOPDIR)/$(APP_INFO)
APP_TITLE := $(shell echo "$(APP_TITLE)" | cut -c1-128)
APP_DESCRIPTION := $(shell echo "$(APP_DESCRIPTION)" | cut -c1-256)
APP_AUTHOR := $(shell echo "$(APP_AUTHOR)" | cut -c1-128)
APP_PRODUCT_CODE := $(shell echo $(APP_PRODUCT_CODE) | cut -c1-16)
APP_UNIQUE_ID := $(shell echo $(APP_UNIQUE_ID) | cut -c1-7)
ifneq ("$(wildcard $(TOPDIR)/$(BANNER_IMAGE).cgfx)","")
	BANNER_IMAGE_FILE := $(TOPDIR)/$(BANNER_IMAGE).cgfx
	BANNER_IMAGE_ARG := -ci $(BANNER_IMAGE_FILE)
else
	BANNER_IMAGE_FILE := $(TOPDIR)/$(BANNER_IMAGE).png
	BANNER_IMAGE_ARG := -i $(BANNER_IMAGE_FILE)
endif

ifneq ("$(wildcard $(TOPDIR)/$(BANNER_AUDIO).cwav)","")
	BANNER_AUDIO_FILE := $(TOPDIR)/$(BANNER_AUDIO).cwav
	BANNER_AUDIO_ARG := -ca $(BANNER_AUDIO_FILE)
else
	BANNER_AUDIO_FILE := $(TOPDIR)/$(BANNER_AUDIO).wav
	BANNER_AUDIO_ARG := -a $(BANNER_AUDIO_FILE)
endif

EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
OUTPUT_NAME := $(subst $(SPACE),,$(APP_TITLE))
OUTPUT_DIR := $(TOPDIR)/$(OUTPUT)
OUTPUT_FILE := $(OUTPUT_DIR)/$(OUTPUT_NAME)

APP_ICON := $(TOPDIR)/$(ICON)
APP_ROMFS := $(TOPDIR)/$(ROMFS)

COMMON_MAKEROM_PARAMS := -rsf $(RSF) -target t -exefslogo -elf $(OUTPUT_FILE).elf -icon icon.icn -banner banner.bnr \
	-DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)" \
	-DAPP_ROMFS="$(APP_ROMFS)" -DAPP_SYSTEM_MODE="64MB" -DAPP_SYSTEM_MODE_EXT="Legacy" -major "$(APP_VER_MAJOR)" \
	-minor "$(APP_VER_MINOR)" -micro "$(APP_VER_MICRO)"

ifeq ($(OS),Windows_NT)
	MAKEROM = makerom.exe
	BANNERTOOL = bannertool.exe
else
	# Prefer the copies committed in tools/bin, so PATH needs no setup.
	MAKEROM = $(firstword $(wildcard $(TOPDIR)/tools/bin/makerom) makerom)
	BANNERTOOL = $(firstword $(wildcard $(TOPDIR)/tools/bin/bannertool) bannertool)
endif

_3DSXFLAGS += --smdh=$(OUTPUT_FILE).smdh
ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(APP_ROMFS)
endif

#---------------------------------------------------------------------------------
# Main Targets
#---------------------------------------------------------------------------------
.PHONY: all 3dsx cia elf 3ds azahar fbi hblauncher sdl-build banner
all: $(OUTPUT_FILE).zip $(OUTPUT_FILE).3ds $(OUTPUT_FILE).cia

banner.bnr: $(BANNER_IMAGE_FILE) $(BANNER_AUDIO_FILE)
	@echo $(BANNER_IMAGE_FILE)
	@$(BANNERTOOL) makebanner $(BANNER_IMAGE_ARG) $(BANNER_AUDIO_ARG) -o banner.bnr > /dev/null

# The long description carries the version, so HOME menu shows which build this is
# (SMDH text fields are 64 characters at most in practice, like ../mzm).
APP_LONG_DESC := $(shell printf '%s %s' "$(APP_TITLE)" "$(VERSION)" | cut -c1-64)

icon.icn: $(TOPDIR)/$(ICON) version.h
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_LONG_DESC)" -p "$(APP_AUTHOR)" -i $(TOPDIR)/$(ICON) -o icon.icn > /dev/null

$(OUTPUT_FILE).elf: $(OFILES)

# The shader header is generated; make sure it exists before its user compiles.
gpu_ppu_3ds.o: gpu_ppu.shbin.o

$(OUTPUT_FILE).3dsx: $(OUTPUT_FILE).elf $(OUTPUT_FILE).smdh

$(OUTPUT_FILE).3ds: $(OUTPUT_FILE).elf banner.bnr icon.icn
	@$(MAKEROM) -f cci -o $(OUTPUT_FILE).3ds -DAPP_ENCRYPTED=true $(COMMON_MAKEROM_PARAMS)
	@echo "built ... $(notdir $@)"

$(OUTPUT_FILE).cia: $(OUTPUT_FILE).elf banner.bnr icon.icn
	@$(MAKEROM) -f cia -o $(OUTPUT_FILE).cia -DAPP_ENCRYPTED=false $(COMMON_MAKEROM_PARAMS)
	@echo "built ... $(notdir $@)"

$(OUTPUT_FILE).zip: $(OUTPUT_FILE).smdh $(OUTPUT_FILE).3dsx
	@cd $(OUTPUT_DIR); \
	mkdir -p 3ds/$(OUTPUT_NAME); \
	cp $(OUTPUT_FILE).3dsx 3ds/$(OUTPUT_NAME); \
	cp $(OUTPUT_FILE).smdh 3ds/$(OUTPUT_NAME); \
	zip -r $(OUTPUT_FILE).zip 3ds > /dev/null; \
	rm -r 3ds
	@echo "built ... $(notdir $@)"

3dsx : $(OUTPUT_FILE).3dsx

cia : $(OUTPUT_FILE).cia

3ds : $(OUTPUT_FILE).3ds

elf : $(OUTPUT_FILE).elf

AZAHAR=flatpak run org.azahar_emu.Azahar
azahar: $(OUTPUT_FILE).3dsx
	$(AZAHAR) $(OUTPUT_FILE).3dsx

fbi: $(OUTPUT_FILE).cia
	python ../buildtools/servefiles.py $(IP3DS) $(OUTPUT_FILE).cia

hblauncher : $(OUTPUT_FILE).3dsx
	3dslink -a $(IP3DS) $(OUTPUT_FILE).3dsx


#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $(patsubst %.shbin.o,%.shbin,$(notdir $@)))
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $@
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
endef

%.shbin.o : %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o : %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)/$(file)))

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
