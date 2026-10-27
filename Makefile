# Makefile for GNU make
####################################################################################################
#
# AUTHOR: DANIEL ZORYCHTA
#
# Version: 20130516
#--------------------------------------------------------------------------------------------------
#
#    Copyright (C) 2011, 2012, 2013  Daniel Zorychta (daniel.zorychta@gmail.com)
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the  Free Software  Foundation;  either version 2 of the License, or
#    any later version.
#
#    This  program  is  distributed  in the hope that  it will be useful,
#    but  WITHOUT  ANY  WARRANTY;  without  even  the implied warranty of
#    MERCHANTABILITY  or  FITNESS  FOR  A  PARTICULAR  PURPOSE.  See  the
#    GNU General Public License for more details.
#
#    You  should  have received a copy  of the GNU General Public License
#    along  with  this  program;  if not,  write  to  the  Free  Software
#    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
#
####################################################################################################

ifeq ($(CONFIG_DIR),)
	CONFIG_DIR=./config
endif

CONFIG=$(CONFIG_DIR)/config.h
include $(CONFIG)

####################################################################################################
# PROJECT CONFIGURATION
####################################################################################################
# project name
PROJECT = $(__PROJECT_NAME__)

#---------------------------------------------------------------------------------------------------
# DEFAULT COMPILER FLAGS
#---------------------------------------------------------------------------------------------------
TOOLCHAIN = $(__PROJECT_TOOLCHAIN__)
CC_WRAPPER = 

# microcontroller specific flags (not used by the POSIX simulator)
ifeq ($(__CPU_ARCH__), posix)
MCU_CFLAGS =
MCU_LFLAGS =
else
MCU_CFLAGS = -mno-unaligned-access
MCU_LFLAGS = -mno-unaligned-access -Wl,-u,vectors -specs=nano.specs -specs=rdimon.specs -Wl,--print-memory-usage
endif

AFLAGS   = -c -MMD -MP \
           -g \
           -ggdb3 \
           $(CPUCONFIG_AFLAGS)

CFLAGS   = -c -MMD -MP \
           -g \
           -ggdb3  \
           -Os \
           -std=c18 \
           -ffunction-sections \
           -Wall \
           -Wextra \
           -Wparentheses \
           -nostdlib\
           $(MCU_CFLAGS) \
           -Werror=implicit-function-declaration \
           -Werror=int-conversion \
           -Werror=incompatible-pointer-types \
           -Werror=type-limits \
           -Werror=return-type \
           -Werror=implicit-int \
           -Werror=parentheses \
           -Werror=enum-compare \
           -Werror=discarded-qualifiers \
           -Werror=override-init \
           -Werror=misleading-indentation \
           -Werror=switch \
           -Werror=stringop-overflow \
           -Werror=overflow \
           -Werror=array-bounds \
           -Wno-enum-conversion \
           -include $(CONFIG) \
           -I./build \
           -I$(CONFIG_DIR) \
           $(CPUCONFIG_CFLAGS)

CXXFLAGS = -c -MMD -MP \
           -g \
           -ggdb3 \
           -Os \
           -std=c++11 \
           -ffunction-sections \
           -fno-rtti \
           -fno-exceptions \
           -fno-unwind-tables \
           $(MCU_CFLAGS) \
           -Wall \
           -Wextra \
           -nostdlib\
           -Wparentheses \
           -Werror=parentheses \
           -Werror=type-limits \
           -Werror=return-type \
           -Werror=enum-compare \
           -Werror=misleading-indentation \
           -Werror=switch \
           -Werror=stringop-overflow \
           -Werror=overflow \
           -Werror=array-bounds \
           -include $(CONFIG) \
           -I./build \
           -I$(CONFIG_DIR) \
           $(CPUCONFIG_CXXFLAGS)

LFLAGS   = -g \
           $(CPUCONFIG_LDFLAGS) \
           -Wl,--gc-sections \
           -Wl,-Map=$(TARGET_PATH)/$(PROJECT).map,--cref,--no-warn-mismatch \
           -Wall \
           $(MCU_LFLAGS) \
           -lm

# flags of files that use host (POSIX simulator) headers and libraries
CFLAGS_HOST = $(CFLAGS) -D_DNX_HOST_ -D_GNU_SOURCE

#---------------------------------------------------------------------------------------------------
# FILE EXTENSIONS CONFIGURATION
#---------------------------------------------------------------------------------------------------
OBJ_EXT = o
C_EXT   = c
CXX_EXT = cpp
AS_EXT  = s

#---------------------------------------------------------------------------------------------------
# FILE AND DIRECTORY NAMES
#---------------------------------------------------------------------------------------------------
# defines project path with binaries
TARGET_DIR_NAME = build
GEN_DRIVERS_DIR = $(TARGET_DIR_NAME)/drivers
GEN_FS_DIR      = $(TARGET_DIR_NAME)/fs

# defines object folder name
OBJ_DIR_NAME    = obj

# dependencies file name
DEP_FILE_NAME   = $(PROJECT).d

# folder localizations
SYS_LOC         = src

SYS_DRV_LOC     = $(SYS_LOC)/drivers
SYS_DRV_INC_LOC = $(SYS_LOC)/include/drivers
SYS_FS_LOC      = $(SYS_LOC)/fs
SYS_INIT_LOC    = $(SYS_LOC)/init
SYS_KRN_LOC     = $(SYS_LOC)/kernel
SYS_LIB_LOC     = $(SYS_LOC)/lib
SYS_MM_LOC      = $(SYS_LOC)/mm
SYS_NET_LOC     = $(SYS_LOC)/net
SYS_CPU_LOC     = $(SYS_LOC)/cpu

#---------------------------------------------------------------------------------------------------
# BASIC PROGRAMS DEFINITIONS
#---------------------------------------------------------------------------------------------------
SHELL      = /bin/bash
ECHO       = /bin/echo -e
RM         = /bin/rm -f
MKDIR      = /bin/mkdir -p
DATE       = /bin/date
CAT        = /bin/cat
GREP       = /bin/grep
UNAME      = /bin/uname -s
SIZEOF     = /usr/bin/stat -c %s
WC         = /usr/bin/wc
PYTHON3    = /usr/bin/python3
CODECHECK  = cppcheck
CC         = $(CC_WRAPPER) $(TOOLCHAIN)gcc
CXX        = $(CC_WRAPPER) $(TOOLCHAIN)g++
LD         = $(TOOLCHAIN)g++
AS         = $(TOOLCHAIN)gcc -x assembler-with-cpp
AR         = $(TOOLCHAIN)ar
OBJCOPY    = $(TOOLCHAIN)objcopy
OBJDUMP    = $(TOOLCHAIN)objdump
SIZE       = $(TOOLCHAIN)size
SYNCDIR    = $(PYTHON3) ./tools/syncdir.py
CONFIGTOOL = $(SHELL) ./tools/configtool.sh
ADDFS      = $(SHELL) ./$(SYS_FS_LOC)/addfs.sh
ADDDRIVERS = $(SHELL) ./$(SYS_DRV_LOC)/adddriver.sh
GIT_HOOKS  = $(SHELL) ./tools/apply_git_hooks.sh
DOXYGEN    = $(SHELL) ./tools/doxygen.sh
RELEASEPKG = $(SHELL) ./tools/releasepkg.sh
RUNGENS    = $(SHELL) ./tools/rungens.sh

#---------------------------------------------------------------------------------------------------
# MAKEFILE CORE (do not edit)
#---------------------------------------------------------------------------------------------------
# defines VALUES
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)

# defines this makefile name
THIS_MAKEFILE = $(firstword $(MAKEFILE_LIST))

# number of threads used in compilation (cpu count)
THREAD = $(shell $(ECHO) $$($(CAT) /proc/cpuinfo | $(GREP) processor | $(WC) -l))

# sets header search path (adds -I flags to paths)
SEARCHPATH          = $(foreach var, $(HDRLOC),-I$(var)) $(foreach var, $(HDRLOC_$(TARGET)),-I$(var))
SEARCHPATH_TARGET   = $(foreach var, $(HDRLOC_$(TARGET)),-I$(var)) -Isrc/
SEARCHPATH_LIB      = $(foreach var, $(_HDRLOC_LIB),-I$(var))
SEARCHPATH_CORE     = $(foreach var, $(_HDRLOC_CORE),-I$(var))
SEARCHPATH_NOARCH   = $(foreach var, $(_HDRLOC_NOARCH),-I$(var))
SEARCHPATH_ARCH     = $(foreach var, $(_HDRLOC_ARCH),-I$(var))
SEARCHPATH_HOST     = -Isrc/ $(foreach var, $(_HDRLOC_HOST),-I$(var))

# main target without defined prefixes
TARGET = $(__CPU_ARCH__)

# target path
TARGET_PATH = $(TARGET_DIR_NAME)/output

# object path
OBJ_PATH = $(TARGET_DIR_NAME)/$(TARGET)/$(OBJ_DIR_NAME)

# list of sources to compile
include $(SYS_LOC)/Makefile

# defines objects localizations
_HDRLOC_CORE     = $(foreach file, $(HDRLOC_CORE),$(SYS_LOC)/$(file))
_HDRLOC_NOARCH   = $(foreach file, $(HDRLOC_NOARCH),$(SYS_LOC)/$(file))
_HDRLOC_ARCH     = $(foreach file, $(HDRLOC_ARCH),$(SYS_LOC)/$(file))
_HDRLOC_HOST     = $(foreach file, $(HDRLOC_HOST),$(SYS_LOC)/$(file))
HDRLOC           = src/ $(_HDRLOC_PROGRAMS) $(_HDRLOC_LIB) $(_HDRLOC_CORE) $(_HDRLOC_NOARCH) $(_HDRLOC_ARCH)

# defines all C sources
_CSRC_CORE       = $(foreach file, $(CSRC_CORE),$(SYS_LOC)/$(file))
_CSRC_NOARCH     = $(foreach file, $(CSRC_NOARCH),$(SYS_LOC)/$(file))
_CSRC_ARCH       = $(foreach file, $(CSRC_ARCH),$(SYS_LOC)/$(file))
_CSRC_HOST       = $(foreach file, $(CSRC_HOST),$(SYS_LOC)/$(file))
CSRC             = $(_CSRC_PROGRAMS) $(_CSRC_LIB) $(_CSRC_CORE) $(_CSRC_NOARCH) $(_CSRC_ARCH)

# defines all C++ sources
_CXXSRC_CORE     = $(foreach file, $(CXXSRC_CORE),$(SYS_LOC)/$(file))
_CXXSRC_NOARCH   = $(foreach file, $(CXXSRC_NOARCH),$(SYS_LOC)/$(file))
_CXXSRC_ARCH     = $(foreach file, $(CXXSRC_ARCH),$(SYS_LOC)/$(file))
CXXSRC           = $(_CXXSRC_PROGRAMS) $(_CXXSRC_LIB) $(_CXXSRC_CORE) $(_CXXSRC_NOARCH) $(_CXXSRC_ARCH)

# defines all assembler sources
ASRC             = $(foreach file, $(ASRC_ARCH),$(SYS_LOC)/$(file))
# convert source files names to objects
CSRCCOREOBJ            = $(_CSRC_CORE:.$(C_EXT)=.$(OBJ_EXT))
CSRCNOARCHOBJ          = $(_CSRC_NOARCH:.$(C_EXT)=.$(OBJ_EXT))
CSRCARCHOBJ            = $(_CSRC_ARCH:.$(C_EXT)=.$(OBJ_EXT))
CSRCHOSTOBJ            = $(_CSRC_HOST:.$(C_EXT)=.$(OBJ_EXT))
CSRCOBJ                = $(CSRC:.$(C_EXT)=.$(OBJ_EXT))
CXXSRCCOREOBJ          = $(_CXXSRC_CORE:.$(CXX_EXT)=.$(OBJ_EXT))
CXXSRCNOARCHOBJ        = $(_CXXSRC_NOARCH:.$(CXX_EXT)=.$(OBJ_EXT))
CXXSRCARCHOBJ          = $(_CXXSRC_ARCH:.$(CXX_EXT)=.$(OBJ_EXT))
CXXSRCOBJ              = $(CXXSRC:.$(CXX_EXT)=.$(OBJ_EXT))
ASRCOBJ                = $(ASRC:.$(AS_EXT)=.$(OBJ_EXT))

# defines objects paths
OBJECTS_CSRC_CORE      = $(foreach var,$(CSRCCOREOBJ),$(OBJ_PATH)/$(var))
OBJECTS_CSRC_NOARCH    = $(foreach var,$(CSRCNOARCHOBJ),$(OBJ_PATH)/$(var))
OBJECTS_CSRC_ARCH      = $(foreach var,$(CSRCARCHOBJ),$(OBJ_PATH)/$(var))
OBJECTS_CSRC_HOST      = $(foreach var,$(CSRCHOSTOBJ),$(OBJ_PATH)/$(var))
OBJECTS_CXXSRC_CORE    = $(foreach var,$(CXXSRCCOREOBJ),$(OBJ_PATH)/$(var))
OBJECTS_CXXSRC_NOARCH  = $(foreach var,$(CXXSRCNOARCHOBJ),$(OBJ_PATH)/$(var))
OBJECTS_CXXSRC_ARCH    = $(foreach var,$(CXXSRCARCHOBJ),$(OBJ_PATH)/$(var))
OBJECTS_ASRC           = $(foreach var,$(ASRCOBJ),$(OBJ_PATH)/$(var))
OBJECTS_CSRC           = $(OBJECTS_CSRC_PROGRAMS) $(OBJECTS_CSRC_LIB) $(OBJECTS_CSRC_CORE) $(OBJECTS_CSRC_NOARCH) $(OBJECTS_CSRC_ARCH)
OBJECTS_CXXSRC         = $(OBJECTS_CXXSRC_PROGRAMS) $(OBJECTS_CXXSRC_LIB) $(OBJECTS_CXXSRC_CORE) $(OBJECTS_CXXSRC_NOARCH) $(OBJECTS_CXXSRC_ARCH)
OBJECTS_ALL            = $(OBJECTS_ASRC) $(OBJECTS_CSRC) $(OBJECTS_CXXSRC) $(OBJECTS_CSRC_HOST)

# architecture dependent sources that do not exist (module not supported by selected CPU),
# modules not supported by selected CPU are also added to UNSUPPORTED_MODULES by module Makefile
ARCH_SRC               = $(_CSRC_ARCH) $(_CXXSRC_ARCH) $(ASRC) $(_CSRC_HOST)
MISSING_ARCH_SRC       = $(filter-out $(wildcard $(ARCH_SRC)),$(ARCH_SRC))

.DELETE_ON_ERROR:

####################################################################################################
# targets
####################################################################################################
.PHONY : all
all : generate apply_git_hooks
	@$(MAKE) -s -j 1 -f$(THIS_MAKEFILE) rungens
	@$(MAKE) -s -j 1 -f$(THIS_MAKEFILE) build_start

.PHONY : build_start
build_start : buildobjects linkobjects hex status

####################################################################################################
# help
####################################################################################################
.PHONY : help
help :
	@$(ECHO) "This is help for this $(THIS_MAKEFILE)"
	@$(ECHO) "Possible targets:"
	@$(ECHO) "   all                 build project (default)"
	@$(ECHO) "   help                this help"
	@$(ECHO) "   config              project configuration (text mode)"
	@$(ECHO) "   clean               clean project"
	@$(ECHO) ""
	@$(ECHO) "Non-build targets:"
	@$(ECHO) "   check               static code analyze by using cppcheck"
	@$(ECHO) "   quickcheck          quick static code analyze by using cppcheck"
	@$(ECHO) "   release             create Release package"
	@$(ECHO) "   doc                 create documentation (Doxygen)"
	@$(ECHO) "   rungens             start generator scripts"

####################################################################################################
# project configuration wizard
####################################################################################################
.PHONY : config
config : clean apply_git_hooks
	@$(CONFIGTOOL) $(CONFIG)

####################################################################################################
# analisis
####################################################################################################
.PHONY : check quickcheck
check :
	@$(CODECHECK) -j $(THREAD) -q --std=c99 --std=c++11 \
	              --enable=warning,style,performance,portability,information,missingInclude \
	              --force --inconclusive \
	              --include=$(CONFIG) -I$(CONFIG_DIR) \
	              $(SEARCHPATH) $(CSRC) $(CXXSRC)

quickcheck :
	@$(CODECHECK) -j $(THREAD) -q --std=c99 --std=c++11 \
	              --enable=warning,style,performance,portability,missingInclude \
	              --force --inconclusive \
	              --include=$(CONFIG) -I$(CONFIG_DIR) -Isrc/include \
	              $(CSRC) $(CXXSRC)

####################################################################################################
# create basic output files like hex, bin, lst etc.
####################################################################################################
.PHONY : hex
hex :
ifeq ($(TARGET), posix)
	@$(ECHO) 'Kernel archive is ready to link with appset (POSIX simulator).'
else
	@$(ECHO) 'Creating IHEX image...'
	@$(OBJCOPY) $(TARGET_PATH)/$(PROJECT).elf -O ihex $(TARGET_PATH)/$(PROJECT).hex

	@$(ECHO) 'Creating binary image...'
	@$(OBJCOPY) $(TARGET_PATH)/$(PROJECT).elf -O binary $(TARGET_PATH)/$(PROJECT).bin

	#@$(ECHO) 'Creating memory dump...'
	#@$(OBJDUMP) -x --syms $(TARGET_PATH)/$(PROJECT).elf > $(TARGET_PATH)/$(PROJECT).dmp

	#@$(ECHO) 'Creating extended listing....'
	#@$(OBJDUMP) -S $(TARGET_PATH)/$(PROJECT).elf > $(TARGET_PATH)/$(PROJECT).lst

	#@$(ECHO) 'Creating objects size list...'
	#@$(SIZE) -B -t --common $(TARGET_PATH)/$(PROJECT).a > $(TARGET_PATH)/$(PROJECT).size
endif

####################################################################################################
# show compile status
####################################################################################################
.PHONY : status
status :
	@$(ECHO) "-----------------------------------"
	@$(ECHO) "| `$(DATE) "+Compilation completed: %H:%M:%S"` |"
	@$(ECHO) "-----------------------------------"

####################################################################################################
####################################################################################################
# Adds programs and libraries to the project
# This target is used to generate ./src/programs/program_registration.c,
# ./src/programs/Makefile, and ./src/lib/Makefile files required in the
# build process
####################################################################################################
.PHONY : generate
generate :
	@$(MKDIR) $(TARGET_PATH)

	@$(ECHO) "Adding file systems to the project..."
	@$(MKDIR) $(GEN_FS_DIR)
	@$(MKDIR) $(GEN_FS_DIR)-tmp
	@$(ADDFS) ./$(SYS_FS_LOC) $(GEN_FS_DIR)-tmp
	@$(SYNCDIR) $(GEN_FS_DIR)-tmp/ $(GEN_FS_DIR)/

	@$(ECHO) "Adding drivers to the project..."
	@$(MKDIR) $(GEN_DRIVERS_DIR)
	@$(MKDIR) $(GEN_DRIVERS_DIR)-tmp
	@$(ADDDRIVERS) ./$(SYS_DRV_LOC) ./$(GEN_DRIVERS_DIR)-tmp $(TARGET) $(CONFIG_DIR)
	@$(SYNCDIR) $(GEN_DRIVERS_DIR)-tmp/ $(GEN_DRIVERS_DIR)/

	@$(ECHO) "Obtaining git hash..."
	@$(MKDIR) $(TARGET_DIR_NAME)/build
	@$(MKDIR) $(TARGET_DIR_NAME)/build-tmp
	@$(ECHO) "#include \"../$(CONFIG)\"" > $(TARGET_DIR_NAME)/build-tmp/defs.h
	@$(ECHO) "#define COMMIT_HASH \"$(shell git rev-parse --short HEAD 2>/dev/null)\"" >> $(TARGET_DIR_NAME)/build-tmp/defs.h
	@$(ECHO) "#define BRANCH_NAME \"$(shell git symbolic-ref --short HEAD 2>/dev/null)\"" >> $(TARGET_DIR_NAME)/build-tmp/defs.h
	@$(ECHO) "#define COMPILE_EPOCH_TIME $(shell $(DATE) "+%s")" > $(TARGET_DIR_NAME)/build-tmp/vars.h
	@$(SYNCDIR) $(TARGET_DIR_NAME)/build-tmp/ $(TARGET_DIR_NAME)/build/

####################################################################################################
# Start all generators
####################################################################################################
.PHONY : rungens
rungens :
	@$(MKDIR) $(TARGET_PATH)
	@$(RUNGENS) $(GENERATOR)

####################################################################################################
# Copy git hooks to git repository
####################################################################################################
.PHONY : apply_git_hooks
apply_git_hooks :
	@$(GIT_HOOKS)

####################################################################################################
# linking rules
####################################################################################################
$(TARGET_PATH)/objects.list : $(OBJECTS_ALL)
	$(file >$@)
	$(foreach f,$(OBJECTS_ALL),$(file >>$@,$(f)))

.PHONY : linkobjects
linkobjects : $(TARGET_PATH)/objects.list
ifeq ($(TARGET), posix)
	@$(ECHO) "Creating kernel archive..."
	@$(RM) $(TARGET_PATH)/$(PROJECT).a
	@$(AR) rcs $(TARGET_PATH)/$(PROJECT).a @$(TARGET_PATH)/objects.list
else
	@$(ECHO) "Linking..."
	@$(LD) @$(TARGET_PATH)/objects.list $(LFLAGS) -o $(TARGET_PATH)/$(PROJECT).elf
endif

####################################################################################################
# POSIX simulator: link kernel archive with appset object
# usage: make link-sim APPSET_OBJ=<appset.o> OUTPUT=<executable> CONFIG_DIR=<dir>
####################################################################################################
POSIX_LD_SYMBOLS = -Wl,--defsym=__text_start=__executable_start \
                   -Wl,--defsym=__text_end=etext \
                   -Wl,--defsym=__ram_start=_posix_boot_stack \
                   -Wl,--defsym=__stack_start=_posix_boot_stack \
                   -Wl,--defsym=__stack_size=$(POSIX_BOOT_STACK_SIZE) \
                   -Wl,--defsym=_appset_data_init_start=0 \
                   -Wl,--defsym=_appset_data_start=0 \
                   -Wl,--defsym=_appset_data_size=0 \
                   -Wl,--defsym=_appset_bss_start=0 \
                   -Wl,--defsym=_appset_bss_size=0

# must be the same as size of _posix_boot_stack (cpu/posix/cpuctl.c) + 512
POSIX_BOOT_STACK_SIZE = 0x10200

.PHONY : link-sim
link-sim :
	@$(ECHO) "Linking POSIX simulator..."
	@$(LD) -Wl,--whole-archive $(TARGET_PATH)/$(PROJECT).a -Wl,--no-whole-archive $(APPSET_OBJ) \
	       $(LFLAGS) $(POSIX_LD_SYMBOLS) $(CPUCONFIG_HOST_LIBS) -o $(OUTPUT)

####################################################################################################
# build objects
####################################################################################################
.PHONY : buildobjects buildobjects_$(TARGET) checkarchsources
buildobjects : checkarchsources
	@$(ECHO) "Starting building objects up to $(THREAD) threads..."
	@$(MAKE) -s -j$(THREAD) -f$(THIS_MAKEFILE) buildobjects_$(TARGET)

buildobjects_$(TARGET) : $(OBJECTS_ALL)

####################################################################################################
# check if all architecture dependent sources exist
####################################################################################################
checkarchsources :
ifneq ($(strip $(UNSUPPORTED_MODULES) $(MISSING_ARCH_SRC)),)
ifneq ($(strip $(UNSUPPORTED_MODULES)),)
	@$(ECHO) "Error: module(s) not supported by the '$(TARGET)' CPU architecture: $(strip $(UNSUPPORTED_MODULES))"
endif
ifneq ($(strip $(MISSING_ARCH_SRC)),)
	@$(foreach f,$(MISSING_ARCH_SRC),$(ECHO) "Error: file '$(f)' does not exist (module not supported by the '$(TARGET)' CPU architecture?).";)
endif
	@$(ECHO) "Disable the module(s) in the project configuration or select other CPU."
	@exit 1
endif

####################################################################################################
# rule used to compile object files from c sources
####################################################################################################
$(OBJ_PATH)/%.$(OBJ_EXT) : %.$(C_EXT) $(THIS_MAKEFILE)
	@$(ECHO) "Compiling: $<"
	@$(MKDIR) $(dir $@)
	@$(CC) $(CFLAGS) $(SEARCHPATH) $< -o $@

####################################################################################################
# rule used to compile object files that use host headers (POSIX simulator)
####################################################################################################
$(OBJECTS_CSRC_HOST) : $(OBJ_PATH)/%.$(OBJ_EXT) : %.$(C_EXT) $(THIS_MAKEFILE)
	@$(ECHO) "Compiling (host): $<"
	@$(MKDIR) $(dir $@)
	@$(CC) $(CFLAGS_HOST) $(SEARCHPATH_HOST) $< -o $@

####################################################################################################
# rule used to compile object files from C++ sources
####################################################################################################
$(OBJ_PATH)/%.$(OBJ_EXT) : %.$(CXX_EXT) $(THIS_MAKEFILE)
	@$(ECHO) "Compiling: $<"
	@$(MKDIR) $(dir $@)
	@$(CXX) $(CXXFLAGS) $(SEARCHPATH) $< -o $@

####################################################################################################
# rule used to compile object files from assembler sources
####################################################################################################
$(OBJ_PATH)/%.$(OBJ_EXT) : %.$(AS_EXT) $(THIS_MAKEFILE)
	@$(ECHO) "Compiling: $<"
	@$(MKDIR) $(dir $@)
	@$(AS) $(AFLAGS) $(SEARCHPATH) $< -o $@

####################################################################################################
# clean all targets
####################################################################################################
.PHONY : clean
clean :
	@$(ECHO) "Deleting all build files..."
	-@$(RM) -r $(TARGET_DIR_NAME) ./doc/doxygen/html/ ./doc/doxygen/latex

####################################################################################################
# target used to create Release archive
####################################################################################################
.PHONY : release
release: clean
	@$(RELEASEPKG)

####################################################################################################
# target used to Doxygen documentation
####################################################################################################
.PHONY : doc
doc:
	$(DOXYGEN)

####################################################################################################
# include all dependencies
####################################################################################################
-include $(OBJECTS_ALL:.o=.d)
