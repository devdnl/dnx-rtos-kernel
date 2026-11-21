# Makefile for GNU make
HDRLOC_ARCH += drivers/urandom

ifeq ($(__ENABLE_URANDOM__), _YES_)
   ifeq ($(TARGET), $(filter $(TARGET), stm32f1 stm32f3 stm32f4 stm32f7 stm32h7))
      CSRC_ARCH   += drivers/urandom/urandom.c
      CSRC_ARCH   += drivers/urandom/stm32fx/urandom_lld.c
   else ifeq ($(TARGET), rpcm0)
      CSRC_ARCH   += drivers/urandom/urandom.c
      CSRC_ARCH   += drivers/urandom/rpcm0/urandom_lld.c
   else ifeq ($(TARGET), posix)
      CSRC_ARCH   += drivers/urandom/urandom.c
      CSRC_ARCH   += drivers/urandom/posix/urandom_lld.c
   else
      UNSUPPORTED_MODULES += URANDOM
   endif
endif
