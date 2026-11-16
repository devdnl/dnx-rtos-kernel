# Makefile for GNU make
HDRLOC_ARCH += drivers/adc

ifeq ($(__ENABLE_ADC__), _YES_)
   ifeq ($(TARGET), $(filter $(TARGET), stm32f1 stm32f3 stm32f4 stm32f7 stm32h7))
      CSRC_ARCH   += drivers/adc/adc.c
      CSRC_ARCH   += drivers/adc/stm32fx/adc_lld.c
   else ifeq ($(TARGET), rpcm0)
      CSRC_ARCH   += drivers/adc/adc.c
      CSRC_ARCH   += drivers/adc/rpcm0/adc_lld.c
   else ifeq ($(TARGET), posix)
      CSRC_ARCH   += drivers/adc/adc.c
      CSRC_ARCH   += drivers/adc/posix/adc_lld.c
   else
      UNSUPPORTED_MODULES += ADC
   endif
endif
