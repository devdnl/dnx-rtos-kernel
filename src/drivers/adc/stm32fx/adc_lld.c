/*==============================================================================
File    adc_lld.c

Author  Daniel Zorychta

Brief   Analog-digital converter, STM32F1, STM32F3, STM32F4, STM32F7, STM32H7

        Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

        This program is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation and modified by the dnx RTOS exception.

        NOTE: The modification  to the GPL is  included to allow you to
              distribute a combined work that includes dnx RTOS without
              being obliged to provide the source  code for proprietary
              components outside of the dnx RTOS.

        The dnx RTOS  is  distributed  in the hope  that  it will be useful,
        but WITHOUT  ANY  WARRANTY;  without  even  the implied  warranty of
        MERCHANTABILITY  or  FITNESS  FOR  A  PARTICULAR  PURPOSE.  See  the
        GNU General Public License for more details.

        Full license text is available on the following file: doc/license.txt.

==============================================================================*/

/*
 * Converter types (selected by registers of the device header):
 *   IP_F1: STM32F1, STM32F37x (CR2 ADON/SWSTART, 12 bits, ADCPRE in RCC)
 *   IP_F4: STM32F4, STM32F7 (CR1 RES, CR2 SWSTART, ADCPRE in common CCR)
 *   IP_F3: STM32F3 (CR ADVREGEN/ADCAL/ADEN/ADSTART, CFGR RES)
 *   IP_H7: STM32H7 (as IP_F3, deep power-down, PCSEL, 16 bits, oversampling)
 */

/*==============================================================================
  Include files
==============================================================================*/
#include "../adc.h"

#if defined(ARCH_stm32f1)
#include "stm32f1/stm32f10x.h"
#include "stm32f1/lib/stm32f10x_rcc.h"
typedef ADC_t ADC_regs_t;
#elif defined(ARCH_stm32f3)
#include "stm32f3/stm32f3xx.h"
#include "stm32f3/lib/stm32f3xx_ll_rcc.h"
typedef ADC_TypeDef ADC_regs_t;
#elif defined(ARCH_stm32f4)
#include "stm32f4/stm32f4xx.h"
#include "stm32f4/lib/stm32f4xx_rcc.h"
typedef ADC_TypeDef ADC_regs_t;
#elif defined(ARCH_stm32f7)
#include "stm32f7/stm32f7xx.h"
#include "stm32f7/lib/stm32f7xx_ll_rcc.h"
typedef ADC_TypeDef ADC_regs_t;
#elif defined(ARCH_stm32h7)
#include "stm32h7/stm32h7xx.h"
#include "stm32h7/lib/stm32h7xx_ll_rcc.h"
typedef ADC_TypeDef ADC_regs_t;
#endif

/*==============================================================================
  Local macros
==============================================================================*/
#if defined(ADC_PCSEL_PCSEL)
#define IP_H7
#elif defined(ADC_CR_ADVREGEN)
#define IP_F3
#elif defined(ADC_CR1_RES)
#define IP_F4
#else
#define IP_F1
#endif

#define TIMEOUT_MS 100
#define NO_CH      _ADC_NO_CHANNEL
#define SMP_MAX    7

#if defined(IP_H7)
#define CHANNELS   20
#define RES_MAX    16
#define OVS_HW_MAX 1024
#define CAL_BITS   16
#define TS_CAL1    (*(const volatile u16_t *)0x1FF1E820)
#define TS_CAL2    (*(const volatile u16_t *)0x1FF1E840)
#elif defined(IP_F3)
#define CHANNELS   19
#define RES_MAX    12
#define OVS_HW_MAX 0
#define CAL_BITS   12
#define TS_CAL1    (*(const volatile u16_t *)0x1FFFF7B8)
#define TS_CAL2    (*(const volatile u16_t *)0x1FFFF7C2)
#elif defined(IP_F4)
#define CHANNELS   19
#define RES_MAX    12
#define OVS_HW_MAX 0
#define CAL_BITS   12
#if defined(TEMPSENSOR_CAL1_ADDR_CMSIS)
#define TS_CAL1 (*TEMPSENSOR_CAL1_ADDR_CMSIS)
#define TS_CAL2 (*TEMPSENSOR_CAL2_ADDR_CMSIS)
#else
#define TS_CAL1 (*(const volatile u16_t *)0x1FFF7A2C)
#define TS_CAL2 (*(const volatile u16_t *)0x1FFF7A2E)
#endif
#if defined(STM32F405xx) || defined(STM32F415xx) || defined(STM32F407xx) || defined(STM32F417xx)
#define TEMP_CH 16
#else
#define TEMP_CH 18
#endif
#else /* IP_F1 */
#define CHANNELS   18
#define RES_MAX    12
#define OVS_HW_MAX 0
#endif

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        ADC_regs_t *REGS;
        volatile uint32_t *ENR;
        uint32_t ENR_BIT;
#if defined(IP_F3) || defined(IP_H7)
        ADC_Common_TypeDef *COMMON;
#endif
        u8_t temp_ch;
        u8_t vref_ch;
} ADC_inst_t;

/*==============================================================================
  Local objects
==============================================================================*/
static const ADC_inst_t INST[] = {
#if defined(IP_F1) && defined(ARCH_stm32f1)
        {ADC1, &RCC->APB2ENR, RCC_APB2ENR_ADC1EN, 16, 17},
#if defined(RCC_APB2ENR_ADC2EN)
        {ADC2, &RCC->APB2ENR, RCC_APB2ENR_ADC2EN, NO_CH, NO_CH},
#endif
#if defined(RCC_APB2ENR_ADC3EN)
        {ADC3, &RCC->APB2ENR, RCC_APB2ENR_ADC3EN, NO_CH, NO_CH},
#endif
#elif defined(IP_F1)
        {ADC1, &RCC->APB2ENR, RCC_APB2ENR_ADC1EN, 16, 17},
#elif defined(IP_F4)
        {ADC1, &RCC->APB2ENR, RCC_APB2ENR_ADC1EN, TEMP_CH, 17},
#if defined(RCC_APB2ENR_ADC2EN)
        {ADC2, &RCC->APB2ENR, RCC_APB2ENR_ADC2EN, NO_CH, NO_CH},
#endif
#if defined(RCC_APB2ENR_ADC3EN)
        {ADC3, &RCC->APB2ENR, RCC_APB2ENR_ADC3EN, NO_CH, NO_CH},
#endif
#elif defined(IP_F3)
#if defined(RCC_AHBENR_ADC12EN)
        {ADC1, &RCC->AHBENR, RCC_AHBENR_ADC12EN, ADC12_COMMON, 16, 18},
#if defined(ADC2)
        {ADC2, &RCC->AHBENR, RCC_AHBENR_ADC12EN, ADC12_COMMON, NO_CH, 18},
#endif
#else
        {ADC1, &RCC->AHBENR, RCC_AHBENR_ADC1EN, ADC1_COMMON, 16, 18},
#endif
#if defined(RCC_AHBENR_ADC34EN)
        {ADC3, &RCC->AHBENR, RCC_AHBENR_ADC34EN, ADC34_COMMON, NO_CH, 18},
        {ADC4, &RCC->AHBENR, RCC_AHBENR_ADC34EN, ADC34_COMMON, NO_CH, 18},
#endif
#elif defined(IP_H7)
        {ADC1, &RCC->AHB1ENR, RCC_AHB1ENR_ADC12EN, ADC12_COMMON, NO_CH, NO_CH},
        {ADC2, &RCC->AHB1ENR, RCC_AHB1ENR_ADC12EN, ADC12_COMMON, NO_CH, NO_CH},
#if !defined(ADC_VER_V5_V90)
        {ADC3, &RCC->AHB4ENR, RCC_AHB4ENR_ADC3EN, ADC3_COMMON, 18, 19},
#endif
#endif
};

/* sample times [cycles x 2] */
#if defined(IP_H7)
static const u16_t SMP_X2[] = {3, 5, 17, 33, 65, 129, 775, 1621};
#elif defined(IP_F3)
static const u16_t SMP_X2[] = {3, 5, 9, 15, 39, 123, 363, 1203};
#elif defined(IP_F4)
static const u16_t SMP_X2[] = {6, 30, 56, 112, 168, 224, 288, 960};
#else
static const u16_t SMP_X2[] = {3, 15, 27, 57, 83, 111, 143, 479};
#endif

static u32_t adc_clk[ARRAY_SIZE(INST)];

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Function waits for register bits.
 * @param  reg          register
 * @param  mask         bits
 * @param  set          true: wait for set bits, false: wait for cleared bits
 * @return One of errno value.
 */
//==============================================================================
static int wait_bits(volatile uint32_t *reg, uint32_t mask, bool set)
{
        clock_t tref = sys_get_uptime_ms();
        u32_t spins = 0;

        while (((*reg & mask) == mask) != set) {
                if (sys_is_time_expired(tref, TIMEOUT_MS)) {
                        return ETIME;
                }

                if (++spins > 1000) {
                        sys_sleep_ms(1);
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function returns frequency of bus clocks.
 */
//==============================================================================
static void bus_clocks(u32_t *hclk, u32_t *pclk2)
{
        LL_RCC_ClocksTypeDef freq;
        LL_RCC_GetSystemClocksFreq(&freq);
        *hclk = freq.HCLK_Frequency;
        *pclk2 = freq.PCLK2_Frequency;
}

//==============================================================================
/**
 * @brief  Function sets sample time of all channels (internal channels:
 *         maximal sample time, required by temperature sensor).
 */
//==============================================================================
static void set_smp(const ADC_inst_t *inst, u8_t code)
{
        u32_t lo = 0, hi = 0;

#if defined(IP_F3)
        const u8_t first = 1; /* no channel 0 */
#else
        const u8_t first = 0;
#endif
        for (u8_t ch = first; ch < 10; ch++) {
                lo |= cast(u32_t, code) << (ch * 3);
        }

        for (u8_t ch = 10; ch < CHANNELS; ch++) {
                u8_t c = ((ch == inst->temp_ch) || (ch == inst->vref_ch)) ? SMP_MAX : code;
                hi |= cast(u32_t, c) << ((ch - 10) * 3);
        }

#if defined(IP_F1) || defined(IP_F4)
        inst->REGS->SMPR2 = lo; /* channels 0..9 */
        inst->REGS->SMPR1 = hi; /* channels 10..18 */
#else
        inst->REGS->SMPR1 = lo; /* channels 0..9 */
        inst->REGS->SMPR2 = hi; /* channels 10..19 */
#endif
}

#if defined(IP_H7)
//==============================================================================
/**
 * @brief  Function checks revision V of STM32H7 (other resolution codes,
 *         converter clock divided by 2, BOOST field).
 */
//==============================================================================
static bool rev_v(void)
{
        return (DBGMCU->IDCODE >> 16) >= 0x2003;
}
#endif

//==============================================================================
/**
 * @brief  Enable converter.
 */
//==============================================================================
int _ADC_LLD__init(u8_t major, ADC_LLD_info_t *info)
{
        if (major >= ARRAY_SIZE(INST)) {
                return ENODEV;
        }

        const ADC_inst_t *inst = &INST[major];
        ADC_regs_t *ADC_ = inst->REGS;
        u32_t hclk, pclk2;
        int err = ESUCC;

        bus_clocks(&hclk, &pclk2);

        info->resolution_max = RES_MAX;
        info->channels = CHANNELS;
        info->temp_channel = inst->temp_ch;
        info->vrefint_channel = inst->vref_ch;
        info->oversampling_hw_max = OVS_HW_MAX;

        SET_BIT(*inst->ENR, inst->ENR_BIT);
        (void)*inst->ENR;

#if defined(IP_F1)
        UNUSED_ARG1(hclk);

        /* ADC clock: PCLK2 / 2, 4, 6, 8, at most 14 MHz (common for all ADCs) */
        u32_t div = 2;
        while ((div < 8) && ((pclk2 / div) > 14000000)) {
                div += 2;
        }
        MODIFY_REG(RCC->CFGR, RCC_CFGR_ADCPRE, ((div / 2) - 1) << 14);
        adc_clk[major] = pclk2 / div;

        ADC_->CR1 = 0;
        ADC_->CR2 = ADC_CR2_ADON;
        sys_sleep_ms(1);

        SET_BIT(ADC_->CR2, ADC_CR2_RSTCAL);
        err = wait_bits(&ADC_->CR2, ADC_CR2_RSTCAL, false);
        if (!err) {
                SET_BIT(ADC_->CR2, ADC_CR2_CAL);
                err = wait_bits(&ADC_->CR2, ADC_CR2_CAL, false);
        }

        /* software start, internal channels */
        SET_BIT(ADC_->CR2, ADC_CR2_EXTSEL | ADC_CR2_EXTTRIG);
        if (inst->temp_ch != NO_CH) {
                SET_BIT(ADC_->CR2, ADC_CR2_TSVREFE);
        }

#elif defined(IP_F4)
        UNUSED_ARG1(hclk);

        /* ADC clock: PCLK2 / 2, 4, 6, 8, at most 36 MHz (common for all ADCs) */
        u32_t div = 2;
        while ((div < 8) && ((pclk2 / div) > 36000000)) {
                div += 2;
        }
        MODIFY_REG(ADC->CCR, ADC_CCR_ADCPRE, ((div / 2) - 1) << 16);
        adc_clk[major] = pclk2 / div;

        if (inst->temp_ch != NO_CH) {
                SET_BIT(ADC->CCR, ADC_CCR_TSVREFE);
        }

        ADC_->CR1 = 0;
        ADC_->CR2 = ADC_CR2_ADON;
        sys_sleep_ms(1);

#elif defined(IP_F3) || defined(IP_H7)
        UNUSED_ARG1(pclk2);

        /* synchronous clock HCLK / 4 (common for ADC pair, set before enable) */
        if (!(inst->COMMON->CCR & ADC_CCR_CKMODE)) {
                SET_BIT(inst->COMMON->CCR, 3U << ADC_CCR_CKMODE_Pos);
        }
        adc_clk[major] = hclk / 4;

        if (inst->temp_ch != NO_CH) {
                SET_BIT(inst->COMMON->CCR, ADC_CCR_TSEN);
        }
        if (inst->vref_ch != NO_CH) {
                SET_BIT(inst->COMMON->CCR, ADC_CCR_VREFEN);
        }

#if defined(IP_H7)
        CLEAR_BIT(ADC_->CR, ADC_CR_DEEPPWD);
        SET_BIT(ADC_->CR, ADC_CR_ADVREGEN);
        sys_sleep_ms(1);

        if (rev_v()) {
                adc_clk[major] /= 2;
#if defined(ADC_CR_BOOST_1)
                MODIFY_REG(ADC_->CR, ADC_CR_BOOST, ADC_CR_BOOST_0 | ADC_CR_BOOST_1);
#endif
        } else {
                SET_BIT(ADC_->CR, ADC_CR_BOOST_0);
        }

        MODIFY_REG(ADC_->CR, ADC_CR_ADCALDIF, ADC_CR_ADCALLIN | ADC_CR_ADCAL);
#else
        CLEAR_BIT(ADC_->CR, ADC_CR_ADVREGEN);
        SET_BIT(ADC_->CR, ADC_CR_ADVREGEN_0);
        sys_sleep_ms(1);

        CLEAR_BIT(ADC_->CR, ADC_CR_ADCALDIF);
        SET_BIT(ADC_->CR, ADC_CR_ADCAL);
#endif
        err = wait_bits(&ADC_->CR, ADC_CR_ADCAL, false);
        if (!err) {
                sys_sleep_ms(1);
                ADC_->ISR = ADC_ISR_ADRDY;
                SET_BIT(ADC_->CR, ADC_CR_ADEN);
                err = wait_bits(&ADC_->ISR, ADC_ISR_ADRDY, true);
        }

        ADC_->CFGR = ADC_CFGR_OVRMOD;
#endif

        if (err) {
                _ADC_LLD__deinit(major);
        }

        return err;
}

//==============================================================================
/**
 * @brief  Disable converter.
 */
//==============================================================================
void _ADC_LLD__deinit(u8_t major)
{
        const ADC_inst_t *inst = &INST[major];
        ADC_regs_t *ADC_ = inst->REGS;

#if defined(IP_F1) || defined(IP_F4)
        ADC_->CR2 = 0;
        CLEAR_BIT(*inst->ENR, inst->ENR_BIT);
#else
        if (ADC_->CR & ADC_CR_ADEN) {
                SET_BIT(ADC_->CR, ADC_CR_ADDIS);
                wait_bits(&ADC_->CR, ADC_CR_ADEN, false);
        }
#if defined(IP_H7)
        CLEAR_BIT(ADC_->CR, ADC_CR_ADVREGEN);
        SET_BIT(ADC_->CR, ADC_CR_DEEPPWD);
#else
        CLEAR_BIT(ADC_->CR, ADC_CR_ADVREGEN);
        SET_BIT(ADC_->CR, ADC_CR_ADVREGEN_1);
#endif
#endif
}

//==============================================================================
/**
 * @brief  Configure converter.
 */
//==============================================================================
int _ADC_LLD__configure(u8_t major, u8_t res, u32_t sample_ns, u16_t oversampling, u32_t *actual_ns)
{
        const ADC_inst_t *inst = &INST[major];
        ADC_regs_t *ADC_ = inst->REGS;

#if defined(IP_F1)
        UNUSED_ARG1(oversampling);
        if (res != 12) {
                return EINVAL;
        }

#elif defined(IP_F4)
        UNUSED_ARG1(oversampling);
        if ((res < 6) || (res > 12) || (res % 2)) {
                return EINVAL;
        }
        MODIFY_REG(ADC_->CR1, ADC_CR1_RES, cast(u32_t, (12 - res) / 2) << 24);

#elif defined(IP_F3)
        UNUSED_ARG1(oversampling);
        if ((res < 6) || (res > 12) || (res % 2)) {
                return EINVAL;
        }
        MODIFY_REG(ADC_->CFGR, ADC_CFGR_RES, cast(u32_t, (12 - res) / 2) << ADC_CFGR_RES_Pos);

#elif defined(IP_H7)
        static const u8_t RES_Y[] = {4, 3, 2, 1, 0}; /* 8, 10, 12, 14, 16 bits */
        static const u8_t RES_V[] = {7, 3, 6, 5, 0};

        if ((res < 8) || (res > 16) || (res % 2)) {
                return EINVAL;
        }

        u8_t code = rev_v() ? RES_V[(res - 8) / 2] : RES_Y[(res - 8) / 2];
        MODIFY_REG(ADC_->CFGR, ADC_CFGR_RES, cast(u32_t, code) << ADC_CFGR_RES_Pos);

        u32_t cfgr2 = 0;
        if (oversampling > 1) {
                u32_t shift = 0;
                while ((1U << shift) < oversampling) {
                        shift++;
                }
                cfgr2 = (cast(u32_t, oversampling - 1) << ADC_CFGR2_OVSR_Pos) | (shift << ADC_CFGR2_OVSS_Pos) |
                        ADC_CFGR2_ROVSE;
        }
        ADC_->CFGR2 = cfgr2;
#endif

        /* sample time: the shortest not shorter than requested */
        u64_t fadc = adc_clk[major];
        u64_t need = ((cast(u64_t, sample_ns) * fadc * 2) + 999999999ULL) / 1000000000ULL;

        u8_t i = 0;
        while ((i < ARRAY_SIZE(SMP_X2) - 1) && (SMP_X2[i] < need)) {
                i++;
        }

        set_smp(inst, i);
        *actual_ns = (cast(u64_t, SMP_X2[i]) * 1000000000ULL) / (fadc * 2);

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Single conversion.
 */
//==============================================================================
int _ADC_LLD__convert(u8_t major, u8_t channel, u32_t *raw)
{
        const ADC_inst_t *inst = &INST[major];
        ADC_regs_t *ADC_ = inst->REGS;

#if defined(IP_F3)
        if (channel == 0) { /* channels 1..18 */
                return ENODEV;
        }
#endif

#if defined(IP_F1) || defined(IP_F4)
        ADC_->SQR1 = 0;
        ADC_->SQR3 = channel;
        ADC_->SR = 0;
        SET_BIT(ADC_->CR2, ADC_CR2_SWSTART);

        int err = wait_bits(&ADC_->SR, ADC_SR_EOC, true);
#else
#if defined(IP_H7)
        SET_BIT(ADC_->PCSEL, 1UL << channel);
#endif
        ADC_->SQR1 = cast(u32_t, channel) << ADC_SQR1_SQ1_Pos;
        ADC_->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
        SET_BIT(ADC_->CR, ADC_CR_ADSTART);

        int err = wait_bits(&ADC_->ISR, ADC_ISR_EOC, true);
#endif
        if (!err) {
                *raw = ADC_->DR;
        }

        return err;
}

//==============================================================================
/**
 * @brief  Temperature calculation.
 */
//==============================================================================
i32_t _ADC_LLD__temperature(u8_t major, u32_t raw, u8_t res, u16_t vref_mV)
{
        UNUSED_ARG1(major);

#if defined(IP_F1)
        /* typical: 1.43 V at 25 C, 4.3 mV/C */
        i64_t uv = (cast(i64_t, raw) * vref_mV * 1000) / ((1 << res) - 1);
        return 25000 + ((1430000 - uv) * 10) / 43;
#else
        /* factory calibration at 30 C and 110 C (Vref+ = 3.3 V) */
        i64_t cal1 = TS_CAL1;
        i64_t cal2 = TS_CAL2;
        i64_t val = (cast(i64_t, raw) * ((1 << CAL_BITS) - 1) * vref_mV) / (cast(i64_t, (1 << res) - 1) * 3300);

        if (cal2 == cal1) {
                return 0;
        }

        return 30000 + ((val - cal1) * 80000) / (cal2 - cal1);
#endif
}

/*==============================================================================
  End of file
==============================================================================*/
