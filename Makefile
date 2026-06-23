#---------------------------------------------------------------------------------
# Clear the implicit built in rules
#---------------------------------------------------------------------------------
.SUFFIXES:
.SECONDARY:
#---------------------------------------------------------------------------------
# prevent deletion of implicit targets
#---------------------------------------------------------------------------------
.SECONDARY:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

GAME_DOL_LOADER := game_dol_loader

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
RELEASE     := $(BUILD)/release
EXTRA_CFLAGS ?=
SOURCES		:=	source source/update source/pngu source/loader shared
DATA		:=  data
TEXTURES	:=	textures
INCLUDES	:=

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

# Find the length of the `patch_dol` function in $(GAME_DOL_LOADER).c and expose it as a macro, which is needed because we memcpy() it.
PATCH_DOL_LEN = 0x$(shell $(DEVKITPPC)/bin/powerpc-eabi-objdump $(GAME_DOL_LOADER).o -t | grep ' patch_dol' | awk '{print $$5}')

CFLAGS		= 	-DPATCH_DOL_LEN=$(PATCH_DOL_LEN) $(EXTRA_CFLAGS) -fno-builtin -g -O2 -Wall $(MACHDEP) $(INCLUDE)
CXXFLAGS	= 	$(CFLAGS)

# compiler flags for the special $(GAME_DOL_LOADER).c file
GAME_DOL_LOADER_CFLAGS = $(EXTRA_CFLAGS) -O2 -fno-builtin -Wall -g $(MACHDEP) $(INCLUDE)

LDFLAGS	=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS	:=	-lcurl -lpng -lfat -lzip -lbz2 -ldi -lmxml -lz -lwiisocket -lmbedtls -lmbedcrypto -lmbedx509 -lwiiuse -lbte -logc -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(DEVKITPRO)/portlibs/wii $(DEVKITPRO)/portlibs/ppc

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
					$(foreach dir,$(TEXTURES),$(CURDIR)/$(dir))
					

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
# dol_loader.c is compiled in its own step and its .o is linked in OFILES
CFILES		:=	$(filter-out $(GAME_DOL_LOADER).c, $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
SCFFILES	:=	$(foreach dir,$(TEXTURES),$(notdir $(wildcard $(dir)/*.scf)))
TPLFILES	:=	$(SCFFILES:.scf=.tpl)

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
					$(addsuffix .o,$(TPLFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
					$(sFILES:.s=.o) $(SFILES:.S=.o) \
					$(GAME_DOL_LOADER).o

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I../shared \
					-I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					-L$(LIBOGC_LIB)

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) build debug beta beta-package clean release run

#---------------------------------------------------------------------------------
$(BUILD):
	make --no-print-directory -C runtime-ext EXTRA_CFLAGS="$(EXTRA_CFLAGS)"
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
debug: EXTRA_CFLAGS := -DRRC_DEBUG=1
debug: export EXTRA_CFLAGS := -DRRC_DEBUG=1
debug: $(BUILD) debug-package

debug-package:
	# Move files to a debug staging directory instead of the release directory
	mkdir -p $(BUILD)/debug/RetroRewindChannel
	mkdir -p $(BUILD)/debug/apps/RetroRewind
	cp runtime-ext/runtime-ext-* $(BUILD)/debug/RetroRewindChannel
	cp $(OUTPUT).dol $(BUILD)/debug/apps/RetroRewind/boot.dol

beta: EXTRA_CFLAGS := -DRRC_BETA=1
beta: export EXTRA_CFLAGS := -DRRC_BETA=1
beta: $(BUILD) beta-package

beta-package:
	# Move files to a beta staging directory instead of the release directory
	mkdir -p $(BUILD)/beta/RetroRewindChannelBeta
	mkdir -p $(BUILD)/beta/apps/RetroRewindBeta
	cp runtime-ext/runtime-ext-* $(BUILD)/beta/RetroRewindChannelBeta
	cp $(OUTPUT).dol $(BUILD)/beta/apps/RetroRewindBeta/boot.dol

clean:
	@echo clean ...
	make --no-print-directory -C runtime-ext clean
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol $(GAME_DOL_LOADER).o
#---------------------------------------------------------------------------------
run:
	wiiload $(OUTPUT).dol

release: $(BUILD)
	# Move files to correct places
	mkdir -p $(RELEASE)/RetroRewindChannel
	mkdir -p $(RELEASE)/apps/RetroRewind
	cp runtime-ext/runtime-ext-* $(RELEASE)/RetroRewindChannel 
	cp $(OUTPUT).dol $(RELEASE)/apps/RetroRewind/boot.dol
	cp assets/THIRD_PARTY_NOTICES.txt $(RELEASE)/THIRD_PARTY_NOTICES.txt

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(GAME_DOL_LOADER).o $(OFILES)

$(GAME_DOL_LOADER).o: $(GAME_DOL_LOADER).c
	$(CC) $(GAME_DOL_LOADER_CFLAGS) -c $< -o $@

#---------------------------------------------------------------------------------
# This rule links in binary data with the .bin extension
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
%.tpl.o	:	%.tpl
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)


-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
