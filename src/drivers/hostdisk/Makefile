# Makefile for GNU make
HDRLOC_ARCH += drivers/hostdisk

ifeq ($(__ENABLE_HOSTDISK__), _YES_)
   ifeq ($(TARGET), posix)
      CSRC_ARCH   += drivers/hostdisk/posix/hostdisk.c
      CXXSRC_ARCH +=
   else
      UNSUPPORTED_MODULES += HOSTDISK
   endif
endif
