# Makefile for GNU make
####################################################################################################
#
# AUTHOR: DANIEL ZORYCHTA
#
# Version: 20120706
#--------------------------------------------------------------------------------------------------
#
#
#    Copyright (C) 2011  Daniel Zorychta (daniel.zorychta@gmail.com)
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

####################################################################################################
# INSERT HERE C SOURCES WHICH MUST BE COMPILED (AUTOMATIC ADDS PATHS AS HEADER PATHS)
####################################################################################################
CSRC = $(sort \
   )

####################################################################################################
# INSERT HERE C++ SOURCES WHICH MUST BE COMPILED (AUTOMATIC ADDS PATHS AS HEADER PATHS)
####################################################################################################
CXXSRC = $(sort \
   )

####################################################################################################
# INSERT HERE ASSEMBLER SOURCES WHICH MUST BE COMPILED (AUTOMATIC ADDS PATHS AS HEADER PATHS)
####################################################################################################
ASRC = $(sort \
        ./boot2_w25q080.S \
   )

####################################################################################################
# INSERT HERE PATHS WITH HEADER FILES ONLY
####################################################################################################
HDRLOC = $(sort $(dir $(CSRC) $(CXXSRC) $(ASRC)) \
        ./asminclude \
   )

####################################################################################################
# PROJECT CONFIGURATION
####################################################################################################
# project name
PROJECT = boot_stage2

OPTIMIZATION = -O3

####################################################################################################
####################################################################################################
# --------------------------------------------------------------------------------------------------
# object extension
OBJ_EXT = o

# c source extension
C_EXT = c

# C++ source extension
CXX_EXT = cpp

# assembler extension
AS_EXT = S

# --------------------------------------------------------------------------------------------------
# defines project path with binaries
BIN_LOC = build

# defines object folder name (localized in the target directory)
OBJ_LOC = objects

# defines listing folder name (localized in the target directory)
LST_LOC = listings

# dependencies file name
DEP_FILE = depends.d

# --------------------------------------------------------------------------------------------------
CC      = arm-none-eabi-gcc
CXX     = arm-none-eabi-g++
LINKER  = arm-none-eabi-gcc
OBJDUMP = arm-none-eabi-objdump
AS      = arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY = arm-none-eabi-objcopy

# --------------------------------------------------------------------------------------------------
# C compiler flags
CFLAGS   = -c \
           -g \
           -ggdb3 \
           -std=c18 \
           -ffunction-sections \
           -fdata-sections \
           -Wall \
           -Wextra \
           -Wparentheses \
           -mno-unaligned-access \
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
           -mcpu=cortex-m0plus \
           $(OPTIMIZATION) \
           -fstack-usage \
           -mthumb

# C++ compiler flags
CXXFLAGS = -c \
           -g \
           -ggdb3 \
           -std=c18 \
           -ffunction-sections \
           -fdata-sections \
           -Wall \
           -Wextra \
           -Wparentheses \
           -mno-unaligned-access \
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
           -mcpu=cortex-m0plus \
           $(OPTIMIZATION) \
           -fstack-usage \
           -mthumb

# linker flags
LFLAGS = -mcpu=cortex-m0plus -nostdlib -T"boot_stage2.ld" \
           -Wl,-Map="$(BIN_LOC)/$(TARGET)/$(TARGET).map" -Wl,--print-memory-usage

# assembler flags
AFLAGS = -mcpu=cortex-m0plus $(OPTIMIZATION) -nostdlib -ffreestanding -mthumb

# --------------------------------------------------------------------------------------------------
# basic program definition
SHELL   = /bin/bash
RM      = /bin/rm -f
MKDIR   = /bin/mkdir -p
DATE    = /bin/date
CAT     = /bin/cat
MKDEP   = /usr/bin/makedepend
WC      = /usr/bin/wc
GREP    = /bin/grep
ECHO    = /bin/echo -e

# --------------------------------------------------------------------------------------------------
# contains the name of this makefile (do not edit)
THIS_MAKEFILE = $(firstword $(MAKEFILE_LIST))

# number of threads when objects are compiled (cpu count + 1) (do not edit)
THREAD = $(shell echo $$[ $$($(CAT) /proc/cpuinfo | $(GREP) processor | $(WC) -l)])

# sets header search path (adds -I flags to paths) (do not edit)
SEARCHPATH = $(foreach var, $(HDRLOC),-I$(var))

# first make goal (do not edit)
FIRST_GOAL = $(firstword $(MAKECMDGOALS))

# main target without defined prefixes (do not edit)
TARGET = rp2040

# target path (do not edit)
TARGET_PATH = $(BIN_LOC)/$(TARGET)

# dependencies path (do not edit)
LST_PATH = $(BIN_LOC)/$(TARGET)/$(LST_LOC)

# object path (do not edit)
OBJ_PATH = $(BIN_LOC)/$(TARGET)/$(OBJ_LOC)

# defines objects names (do not edit)
OBJECTS = $(ASRC:.$(AS_EXT)=.$(OBJ_EXT)) \
          $(CSRC:.$(C_EXT)=.$(OBJ_EXT))  \
          $(CXXSRC:.$(CXX_EXT)=.$(OBJ_EXT))


####################################################################################################
####################################################################################################
# default target
####################################################################################################
.PHONY : all
all :
	@$(MAKE) -s -f$(THIS_MAKEFILE) f042

####################################################################################################
# targets
####################################################################################################
.PHONY : f042
f042 : dependencies buildobjects linkobjects status

####################################################################################################
# show compile status
####################################################################################################
.PHONY : status
status :
	@$(ECHO) "-----------------------------------"
	@$(ECHO) "| `$(DATE) "+Compilation completed: %H:%M:%S"` |"
	@$(ECHO) "-----------------------------------"

####################################################################################################
# makes dependences
####################################################################################################
.PHONY : dependencies
dependencies:
	@echo "Creating dependencies..."
	@$(MKDIR) $(TARGET_PATH)
	@echo "" > $(TARGET_PATH)/$(DEP_FILE)
	@$(DEPAPP) -f$(TARGET_PATH)/$(DEP_FILE) -p$(OBJ_PATH)/ -o.$(OBJ_EXT) $(SEARCHPATH) -Y -- $(CFLAGS) -- $(CSRC) $(CXXSRC) >/dev/null 2>&1
	@echo -e "$(foreach var,$(CSRC),\n$(OBJ_PATH)/$(var:.$(C_EXT)=.$(OBJ_EXT)) : $(var))" >> $(TARGET_PATH)/$(DEP_FILE)
	@echo -e "$(foreach var,$(CXXSRC),\n$(OBJ_PATH)/$(var:.$(CXX_EXT)=.$(OBJ_EXT)) : $(var))" >> $(TARGET_PATH)/$(DEP_FILE)
	@echo -e "$(foreach var,$(ASRC),\n$(OBJ_PATH)/$(var:.$(AS_EXT)=.$(OBJ_EXT)) : $(var))" >> $(TARGET_PATH)/$(DEP_FILE)

####################################################################################################
# linking rules
####################################################################################################
.PHONY : linkobjects
linkobjects :
	@echo "Linking..."
	@$(MKDIR) $(TARGET_PATH)
	@$(RM) $(TARGET_PATH)/*.*
	@$(LINKER) $(LFLAGS) $(foreach var,$(OBJECTS),$(OBJ_PATH)/$(var)) -o $(TARGET_PATH)/$(PROJECT).elf
	
	@$(ECHO) 'Creating listing...'
	@$(OBJDUMP) -S $(TARGET_PATH)/$(PROJECT).elf > $(TARGET_PATH)/$(PROJECT).lst

	@$(ECHO) 'Creating IHEX image...'
	@$(OBJCOPY) $(TARGET_PATH)/$(PROJECT).elf -O ihex $(TARGET_PATH)/$(PROJECT).hex

	@$(ECHO) 'Creating binary image...'
	@$(OBJCOPY) $(TARGET_PATH)/$(PROJECT).elf -O binary $(TARGET_PATH)/$(PROJECT).bin

	@$(ECHO) 'Creating BOOT2 image...'
	@./pad_checksum $(TARGET_PATH)/$(PROJECT).bin $(TARGET_PATH)/$(PROJECT).img

####################################################################################################
# build objects
####################################################################################################
.PHONY : buildobjects
buildobjects :
	@echo "Starting building objects on $(THREAD) threads..."
	@$(MAKE) -s -j$(THREAD) -f$(THIS_MAKEFILE) buildobjects_$(TARGET)

.PHONY : buildobjects_$(TARGET)
buildobjects_$(TARGET) : $(foreach var,$(OBJECTS),$(OBJ_PATH)/$(var))

####################################################################################################
# rule used to compile object files from c sources
####################################################################################################
$(OBJ_PATH)/%.$(OBJ_EXT) : %.$(C_EXT)
	@echo "Building: $<..."
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) $(SEARCHPATH) $(subst $(OBJ_PATH)/,,$(@:.$(OBJ_EXT)=.$(C_EXT))) -o $@

####################################################################################################
# rule used to compile object files from C++ sources
####################################################################################################
$(OBJ_PATH)/%.$(OBJ_EXT) : %.$(CXX_EXT)
	@echo "Building: $<..."
	@$(MKDIR) $(dir $@)
	$(CXX) $(CXXFLAGS) $(SEARCHPATH) $(subst $(OBJ_PATH)/,,$(@:.$(OBJ_EXT)=.$(CXX_EXT))) -o $@

####################################################################################################
# rule used to compile object files from assembler sources
####################################################################################################
$(OBJ_PATH)/%.$(OBJ_EXT) : %.$(AS_EXT)
	@echo "Building: $<..."
	@$(MKDIR) $(dir $@)
	@$(AS) $(AFLAGS) $(SEARCHPATH) $(subst $(OBJ_PATH)/,,$(@:.$(OBJ_EXT)=.$(AS_EXT))) -o $@

####################################################################################################
# clean target
####################################################################################################
.PHONY : cleantarget
cleantarget :
	@echo "Cleaning target..."
	-@$(RM) -r $(OBJ_PATH) $(LST_PATH)
	-@$(RM) $(BIN_LOC)/*.*

####################################################################################################
# clean all targets
####################################################################################################
.PHONY : clean
clean :
	@echo "Deleting all build files..."
	-@$(RM) -r $(BIN_LOC)

####################################################################################################
# flash target CPU by using ./tools/flash.sh script
####################################################################################################
.PHONY : install
install :
	./tools/flash.sh ../$(TARGET_PATH)/$(PROJECT).elf

####################################################################################################
# include all dependencies
####################################################################################################
-include $(TARGET_PATH)/$(DEP_FILE)
