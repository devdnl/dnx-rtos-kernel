# Makefile for GNU make
HDRLOC_ARCH += drivers/qspi

ifeq ($(__ENABLE_QSPI__), _YES_)
   ifeq ($(TARGET), stm32f4)
      CSRC_ARCH   += drivers/qspi/$(TARGET)/qspi.c
      CSRC_ARCH   += drivers/qspi/$(TARGET)/stm32f4xx_hal_qspi.c
      CXXSRC_ARCH +=
   endif
   ifeq ($(TARGET), stm32h7)
      CSRC_ARCH   += drivers/qspi/$(TARGET)/qspi.c
      CSRC_ARCH   += drivers/qspi/$(TARGET)/stm32h7xx_hal_qspi.c
      CXXSRC_ARCH +=
   endif
endif
