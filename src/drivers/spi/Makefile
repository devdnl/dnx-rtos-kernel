# Makefile for GNU make
HDRLOC_ARCH += drivers/spi

ifeq ($(__ENABLE_SPI__), _YES_)
  CSRC_ARCH   += drivers/spi/spi.c
  ifeq ($(TARGET), stm32f1)
      CSRC_ARCH   += drivers/spi/stm32fx/spi_lld.c
      CXXSRC_ARCH += 
  else ifeq ($(TARGET), stm32f4)
      CSRC_ARCH   += drivers/spi/stm32fx/spi_lld.c
      CXXSRC_ARCH += 
  else ifeq ($(TARGET), stm32f7)
      CSRC_ARCH   += drivers/spi/stm32fx/spi_lld.c
      CXXSRC_ARCH += 
  else ifeq ($(TARGET), stm32h7)
      CSRC_ARCH   += drivers/spi/stm32h7/spi_lld.c
      CSRC_ARCH   += drivers/spi/stm32h7/stm32h7xx_hal_spi_ex.c
      CSRC_ARCH   += drivers/spi/stm32h7/stm32h7xx_hal_spi.c
      CSRC_ARCH   += drivers/spi/stm32h7/stm32h7xx_ll_spi.c
      CXXSRC_ARCH += 
  else
      UNSUPPORTED_MODULES += SPI
  endif
endif
