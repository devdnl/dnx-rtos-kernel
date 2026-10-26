# Makefile for GNU make
HDRLOC_ARCH += drivers/adc

ifeq ($(__ENABLE_ADC__), _YES_)
   ifeq ($(TARGET), stm32f1)
      CSRC_ARCH   += drivers/adc/$(TARGET)/adc.c
      CXXSRC_ARCH +=
   else
      UNSUPPORTED_MODULES += ADC
   endif
endif
