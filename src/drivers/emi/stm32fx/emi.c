/*==============================================================================
File     emi.c

Author   Daniel Zorychta

Brief    External Memory Interface

         Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

         This program is free software; you can redistribute it and/or modify
         it under the terms of the GNU General Public License version 2 as
         published by the Free Software Foundation.

         The dnx RTOS is distributed in the hope that it will be useful,
         but WITHOUT ANY WARRANTY; without even the implied warranty of
         MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
         GNU General Public License for more details.

         Programs using the kernel by system calls are not covered by the license,
         drivers and file systems of drvset partition: exception, see the license
         file: doc/license.txt.


==============================================================================*/

// TODO Driver is not completed!
// #pragma GCC optimize("Og")

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "../emi_ioctl.h"

#if defined(ARCH_stm32f1)
#error No FMC implementation!
#elif defined(ARCH_stm32f4)
#include "stm32f4/stm32f4xx.h"
#include "stm32f4/lib/stm32f4xx_rcc.h"
#elif defined(ARCH_stm32f7)
#include "stm32f7/stm32f7xx.h"
#include "stm32f7/lib/stm32f7xx_ll_rcc.h"
#elif defined(ARCH_stm32h7)
#include "stm32h7/stm32h7xx.h"
#include "stm32h7/lib/stm32h7xx_ll_rcc.h"
#endif

/*==============================================================================
  Local macros
==============================================================================*/
#define SDRAM_CMD__NORMAL        0x0
#define SDRAM_CMD__CLK_CFG_EN    0x1
#define SDRAM_CMD__PRECHARGE_ALL 0x2
#define SDRAM_CMD__AUTOREFRESH   0x3
#define SDRAM_CMD__LOADMODEREG   0x4
#define SDRAM_CMD__SELFREFRESH   0x5
#define SDRAM_CMD__POWERDOWN     0x6

#define SDRAM_MODE_REG_BURST_LENGTH_Pos       0
#define SDRAM_MODE_REG_BURST_LENGTH_1         (0 << SDRAM_MODE_REG_BURST_LENGTH_Pos)
#define SDRAM_MODE_REG_BURST_LENGTH_2         (1 << SDRAM_MODE_REG_BURST_LENGTH_Pos)
#define SDRAM_MODE_REG_BURST_LENGTH_4         (2 << SDRAM_MODE_REG_BURST_LENGTH_Pos)
#define SDRAM_MODE_REG_BURST_LENGTH_8         (3 << SDRAM_MODE_REG_BURST_LENGTH_Pos)
#define SDRAM_MODE_REG_BURST_LENGTH_FULL_PAGE (7 << SDRAM_MODE_REG_BURST_LENGTH_Pos)

#define SDRAM_MODE_REG_BURST_TYPE_Pos         3
#define SDRAM_MODE_REG_BURST_TYPE_SEQUENTIAL  (0 << SDRAM_MODE_REG_BURST_TYPE_Pos)
#define SDRAM_MODE_REG_BURST_TYPE_INTERLEAVED (1 << SDRAM_MODE_REG_BURST_TYPE_Pos)

#define SDRAM_MODE_REG_CAS_LATENCY_Pos 4
#define SDRAM_MODE_REG_CAS_LATENCY_1   (1 << SDRAM_MODE_REG_CAS_LATENCY_Pos)
#define SDRAM_MODE_REG_CAS_LATENCY_2   (2 << SDRAM_MODE_REG_CAS_LATENCY_Pos)
#define SDRAM_MODE_REG_CAS_LATENCY_3   (3 << SDRAM_MODE_REG_CAS_LATENCY_Pos)

#define SDRAM_MODE_REG_OPERATING_MODE_Pos      7
#define SDRAM_MODE_REG_OPERATING_MODE_STANDARD (0 << SDRAM_MODE_REG_OPERATING_MODE_Pos)

#define SDRAM_MODE_REG_WRITE_BURST_MODE_Pos                    9
#define SDRAM_MODE_REG_WRITE_BURST_MODE_PROG_LENGTH            (0 << SDRAM_MODE_REG_WRITE_BURST_MODE_Pos)
#define SDRAM_MODE_REG_WRITE_BURST_MODE_SINGLE_LOCATION_ACCESS (1 << SDRAM_MODE_REG_WRITE_BURST_MODE_Pos)

#define FMC_BCRx_RESET_VALUE 0x000030D2

#define SDRAM_BENCH_BURST (8 * sizeof(u32_t))
#define SDRAM_BENCH_CHUNK (64 * 1024)

#define SDRAM_TEST_SEED         0x2545F491
#define SDRAM_TEST_RETENTION_MS 500

#if defined(ARCH_stm32f1)
#error No FMC implementation!
#elif defined(ARCH_stm32f4)
#define FMC_Bank1_R  FMC_Bank1
#define FMC_BANK_5_6 FMC_Bank5_6
#define FMC_ENABLE()
#define SDRAM1_ADDRESS       0xC0000000
#define SDRAM2_ADDRESS       0xD0000000
#define FMC_SDCRx_NC_Pos     (0U)
#define FMC_SDCRx_NR_Pos     (2U)
#define FMC_SDCRx_MWID_Pos   (4U)
#define FMC_SDCRx_NB_Pos     (6U)
#define FMC_SDCRx_CAS_Pos    (7U)
#define FMC_SDCRx_SDCLK_Pos  (10U)
#define FMC_SDCRx_RBURST_Pos (12U)
#define FMC_SDCRx_RPIPE_Pos  (13U)
#define FMC_SDTRx_TMRD_Pos   (0U)
#define FMC_SDTRx_TXSR_Pos   (4U)
#define FMC_SDTRx_TRAS_Pos   (8U)
#define FMC_SDTRx_TRC_Pos    (12U)
#define FMC_SDTRx_TWR_Pos    (16U)
#define FMC_SDTRx_TRP_Pos    (20U)
#define FMC_SDTRx_TRCD_Pos   (24U)
#elif defined(ARCH_stm32f7)
#define FMC_Bank1_R  FMC_Bank1
#define FMC_BANK_5_6 FMC_Bank5_6
#define FMC_ENABLE()
#define SDRAM1_ADDRESS       0xC0000000
#define SDRAM2_ADDRESS       0xD0000000
#define FMC_SDCRx_NC_Pos     (0U)
#define FMC_SDCRx_NR_Pos     (2U)
#define FMC_SDCRx_MWID_Pos   (4U)
#define FMC_SDCRx_NB_Pos     (6U)
#define FMC_SDCRx_CAS_Pos    (7U)
#define FMC_SDCRx_SDCLK_Pos  (10U)
#define FMC_SDCRx_RBURST_Pos (12U)
#define FMC_SDCRx_RPIPE_Pos  (13U)
#define FMC_SDTRx_TMRD_Pos   (0U)
#define FMC_SDTRx_TXSR_Pos   (4U)
#define FMC_SDTRx_TRAS_Pos   (8U)
#define FMC_SDTRx_TRC_Pos    (12U)
#define FMC_SDTRx_TWR_Pos    (16U)
#define FMC_SDTRx_TRP_Pos    (20U)
#define FMC_SDTRx_TRCD_Pos   (24U)
#elif defined(ARCH_stm32h7)
#define FMC_BANK_5_6   FMC_Bank5_6_R
#define FMC_ENABLE()   SET_BIT(FMC_Bank1_R->BTCR[0], FMC_BCR1_FMCEN)
#define SDRAM1_ADDRESS 0xC0000000
#define SDRAM2_ADDRESS 0xD0000000
#endif

#if (__EMI_SDRAM_1_ENABLE__ or __EMI_SDRAM_2_ENABLE__)
/*
 * SDRAM timings: conversion of datasheet times [ns] to SDCLK cycles (rounded up).
 * All values are calculated at compile time.
 */
#define SDRAM_MAX(_a, _b) ((_a) > (_b) ? (_a) : (_b))
#define SDRAM_MIN(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#define SDRAM_NS2CLK(_ns) SDRAM_MAX((((_ns) * __EMI_SDRAM_FREQ_MHZ__) + 999) / 1000, 1)

#define SDRAM_TRP    SDRAM_NS2CLK(__EMI_SDRAM_TRP_NS__)
#define SDRAM_TRC    SDRAM_NS2CLK(__EMI_SDRAM_TRC_NS__)
#define SDRAM_TRAS   SDRAM_NS2CLK(__EMI_SDRAM_TRAS_NS__)
#define SDRAM_TXSR   SDRAM_NS2CLK(__EMI_SDRAM_TXSR_NS__)
#define SDRAM_TMRD   (__EMI_SDRAM_TMRD__)
#define SDRAM_1_TRCD SDRAM_NS2CLK(__EMI_SDRAM_1_TRCD_NS__)
#define SDRAM_2_TRCD SDRAM_NS2CLK(__EMI_SDRAM_2_TRCD_NS__)

/* the shortest tRCD of enabled banks */
#if (__EMI_SDRAM_1_ENABLE__ and __EMI_SDRAM_2_ENABLE__)
#define SDRAM_TRCD_MIN SDRAM_MIN(SDRAM_1_TRCD, SDRAM_2_TRCD)
#elif __EMI_SDRAM_1_ENABLE__
#define SDRAM_TRCD_MIN SDRAM_1_TRCD
#else
#define SDRAM_TRCD_MIN SDRAM_2_TRCD
#endif

/*
 * tWR is common for both banks and FMC requires: tWR >= tRAS - tRCD and
 * tWR >= tRC - tRCD - tRP (see reference manual, FMC_SDTRx register).
 */
#define SDRAM_TWR_DEVICE SDRAM_MAX(SDRAM_NS2CLK(__EMI_SDRAM_TWR_NS__), __EMI_SDRAM_TWR_MIN_CLK__)
#define SDRAM_TWR_FMC    SDRAM_MAX(SDRAM_TRAS - SDRAM_TRCD_MIN, SDRAM_TRC - SDRAM_TRCD_MIN - SDRAM_TRP)
#define SDRAM_TWR        SDRAM_MAX(SDRAM_TWR_DEVICE, SDRAM_TWR_FMC)

/* FMC supports 1..16 cycles */
#if SDRAM_TRP > 16
#error "EMI: SDRAM tRP exceeds 16 cycles!"
#endif
#if SDRAM_TRC > 16
#error "EMI: SDRAM tRC exceeds 16 cycles!"
#endif
#if SDRAM_TRAS > 16
#error "EMI: SDRAM tRAS exceeds 16 cycles!"
#endif
#if SDRAM_TXSR > 16
#error "EMI: SDRAM tXSR exceeds 16 cycles!"
#endif
#if SDRAM_TWR > 16
#error "EMI: SDRAM tWR exceeds 16 cycles!"
#endif
#if (SDRAM_TMRD < 1) or (SDRAM_TMRD > 16)
#error "EMI: SDRAM tMRD out of range 1..16 cycles!"
#endif
#if __EMI_SDRAM_1_ENABLE__ and (SDRAM_1_TRCD > 16)
#error "EMI: SDRAM1 tRCD exceeds 16 cycles!"
#endif
#if __EMI_SDRAM_2_ENABLE__ and (SDRAM_2_TRCD > 16)
#error "EMI: SDRAM2 tRCD exceeds 16 cycles!"
#endif
#endif

/*==============================================================================
  Local object types
==============================================================================*/
MODULE_NAME(EMI);

/*==============================================================================
  Local function prototypes
==============================================================================*/
static int sdram_init(void);
static int register_heap_regions(u32_t mem_addr, size_t mem_size, const char *opts);

/*==============================================================================
  Local object
==============================================================================*/

/*==============================================================================
  Exported object
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief Initialize device.
 *
 * @param[out]          **device_handle        device allocated memory
 * @param[in ]            major                major device number
 * @param[in ]            minor                minor device number
 * @param[in ]            config               optional module configuration
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_INIT(EMI, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        UNUSED_ARG2(device_handle, config);

        int err = EFAULT;

        if (major == 0 && minor == 0) {
                SET_BIT(RCC->AHB3ENR, RCC_AHB3ENR_FMCEN);
                volatile u32_t tmpreg = READ_BIT(RCC->AHB3ENR, RCC_AHB3ENR_FMCEN);
                UNUSED_ARG1(tmpreg);

                SET_BIT(RCC->AHB3RSTR, RCC_AHB3RSTR_FMCRST);
                CLEAR_BIT(RCC->AHB3RSTR, RCC_AHB3RSTR_FMCRST);

                /*
                 * AN4861 5.6.1 Disable FMC Bank1 if not used
                 */
                FMC_Bank1_R->BTCR[0] = FMC_BCRx_RESET_VALUE;
                FMC_Bank1_R->BTCR[1] = FMC_BCRx_RESET_VALUE;
                FMC_Bank1_R->BTCR[2] = FMC_BCRx_RESET_VALUE;
                FMC_Bank1_R->BTCR[3] = FMC_BCRx_RESET_VALUE;

#if (__EMI_SDRAM_1_ENABLE__ or __EMI_SDRAM_2_ENABLE__)
                err = sdram_init();
#endif
        }

        return err;
}

//==============================================================================
/**
 * @brief Release device.
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_RELEASE(EMI, void *device_handle)
{
        UNUSED_ARG1(device_handle);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Open device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags (O_RDONLY, O_WRONLY, O_RDWR)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_OPEN(EMI, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(device_handle, flags);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Close device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           force                  device force close (true)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_CLOSE(EMI, void *device_handle, bool force)
{
        UNUSED_ARG2(device_handle, force);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Write data to device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]          *src                    data source
 * @param[in ]           count                  number of bytes to write
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *wrcnt                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_WRITE(EMI, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos, size_t *wrcnt,
        struct vfs_fattr fattr)
{
        UNUSED_ARG6(device_handle, src, count, fpos, wrcnt, fattr);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Read data from device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdcnt                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_READ(EMI, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos, size_t *rdcnt, struct vfs_fattr fattr)
{
        UNUSED_ARG6(device_handle, dst, count, fpos, rdcnt, fattr);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief IO control.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           request                request
 * @param[in ][out]     *arg                    request's argument
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_IOCTL(EMI, void *device_handle, int request, void *arg)
{
        UNUSED_ARG3(device_handle, request, arg);
        return ENOTSUP;
}

//==============================================================================
/**
 * @brief Flush device.
 *
 * @param[in ]          *device_handle          device allocated memory
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_FLUSH(EMI, void *device_handle)
{
        UNUSED_ARG1(device_handle);
        return ESUCC;
}

//==============================================================================
/**
 * @brief Device information.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *device_stat            device status
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_STAT(EMI, void *device_handle, struct vfs_dev_stat *device_stat)
{
        UNUSED_ARG1(device_handle);

        device_stat->st_size = 0;

        return ESUCC;
}

#if (__EMI_SDRAM_1_ENABLE__ or __EMI_SDRAM_2_ENABLE__)
#if __EMI_SDRAM_TEST_ENABLE__ == _YES_
//==============================================================================
/**
 * @brief  Get CPU core frequency (frequency of the DWT cycle counter).
 *
 * @return Frequency in Hz.
 */
//==============================================================================
static u32_t sdram_get_cpu_frequency(void)
{
        LL_RCC_ClocksTypeDef freq;
        LL_RCC_GetSystemClocksFreq(&freq);

#if defined(ARCH_stm32h7)
        return LL_RCC_CALC_SYSCLK_FREQ(freq.SYSCLK_Frequency, LL_RCC_GetSysPrescaler());
#else
        return freq.HCLK_Frequency;
#endif
}

//==============================================================================
/**
 * @brief  Measure number of CPU cycles of block write or read. Transfers are
 *         done by 8-word bursts, thus memory is the bottleneck, not the CPU.
 *         Interrupts are disabled only for a single chunk.
 *
 * @param  address      address
 * @param  size         size (bytes), multiple of SDRAM_BENCH_CHUNK
 * @param  write        true: write, false: read
 *
 * @return Number of CPU cycles.
 */
//==============================================================================
static u64_t sdram_measure_cycles(void *address, size_t size, bool write)
{
        u64_t cycles = 0;
        u32_t *ptr = address;

        for (size_t chunk = 0; chunk < (size / SDRAM_BENCH_CHUNK); chunk++) {

                u32_t n = SDRAM_BENCH_CHUNK / SDRAM_BENCH_BURST;

                sys_critical_section_begin();
                u32_t start = DWT->CYCCNT;

                if (write) {
                        /* written values do not matter, registers are only reserved */
                        __asm volatile("1: stmia %[ptr]!, {r3, r4, r5, r6, r8, r9, r10, r12} \n"
                                       "   subs %[n], #1                                    \n"
                                       "   bne 1b                                           \n"
                                       "   dsb                                              \n"
                                       : [ptr] "+r"(ptr), [n] "+r"(n)
                                       :
                                       : "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r12", "cc", "memory");
                } else {
                        __asm volatile("1: ldmia %[ptr]!, {r3, r4, r5, r6, r8, r9, r10, r12} \n"
                                       "   subs %[n], #1                                    \n"
                                       "   bne 1b                                           \n"
                                       "   dsb                                              \n"
                                       : [ptr] "+r"(ptr), [n] "+r"(n)
                                       :
                                       : "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r12", "cc", "memory");
                }

                cycles += DWT->CYCCNT - start;
                sys_critical_section_end();
        }

        return cycles;
}

//==============================================================================
/**
 * @brief  Measure and print SDRAM write and read speed. Memory content is lost.
 *
 * @param  address      address
 * @param  size         size (bytes)
 */
//==============================================================================
static void sdram_benchmark(void *address, size_t size)
{
        /* enable cycle counter */
        SET_BIT(CoreDebug->DEMCR, CoreDebug_DEMCR_TRCENA_Msk);
#if defined(ARCH_stm32f7) or defined(ARCH_stm32h7)
        *cast(volatile u32_t *, DWT_BASE + 0xFB0) = 0xC5ACCE55; // unlock DWT (LAR)
#endif
        SET_BIT(DWT->CTRL, DWT_CTRL_CYCCNTENA_Msk);

        u64_t cpu_hz = sdram_get_cpu_frequency();
        u64_t wr = sdram_measure_cycles(address, size, true);
        u64_t rd = sdram_measure_cycles(address, size, false);

        if (wr and rd) {
                dev_dbg2(0, 0, "memory write: %u MB/s, read: %u MB/s.", cast(u32_t, size * cpu_hz / wr / 1000000),
                        cast(u32_t, size * cpu_hz / rd / 1000000));
        }
}

//==============================================================================
/**
 * @brief  Pseudo-random sequence generator (xorshift32).
 *
 * @param  x            previous value (not 0)
 *
 * @return Next value.
 */
//==============================================================================
static u32_t sdram_random(u32_t x)
{
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return x;
}

//==============================================================================
/**
 * @brief  Test data lines (walking one and zero) and address lines (aliasing).
 *
 * @param  address      address
 * @param  size         size (bytes)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int sdram_test_lines(void *address, size_t size)
{
        volatile u32_t *mem = address;
        size_t words = size / sizeof(u32_t);

        /* the second write drives the bus by the opposite value */
        for (u32_t bit = 1; bit != 0; bit <<= 1) {
                mem[0] = bit;
                mem[1] = ~bit;
                if ((mem[0] != bit) or (mem[1] != ~bit)) {
                        dev_dbg2(0, 0, "data line error, pattern %08lXh", bit);
                        return EFAULT;
                }
        }

        /* each address line selects a different cell */
        mem[0] = 0;
        for (size_t offset = 1; offset < words; offset <<= 1) {
                mem[offset] = offset;
        }

        if (mem[0] != 0) {
                dev_dbg2(0, 0, "address line error @ %p", mem);
                return EFAULT;
        }

        for (size_t offset = 1; offset < words; offset <<= 1) {
                if (mem[offset] != offset) {
                        dev_dbg2(0, 0, "address line error @ %p", &mem[offset]);
                        return EFAULT;
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Test 8 and 16-bit accesses (byte mask lines).
 *
 * @param  address      address
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int sdram_test_byte_lanes(void *address)
{
        volatile u32_t *mem32 = address;
        volatile u16_t *mem16 = address;
        volatile u8_t *mem8 = address;

        mem32[0] = 0xFFFFFFFF;
        mem32[1] = 0xFFFFFFFF;

        for (u8_t i = 0; i < 4; i++) {
                mem8[i] = 0x10 + i;
        }

        mem16[2] = 0x5A01;
        mem16[3] = 0xA502;

        if ((mem32[0] != 0x13121110) or (mem32[1] != 0xA5025A01)) {
                dev_dbg2(0, 0, "byte access error @ %p", address);
                return EFAULT;
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Write pseudo-random pattern to whole memory.
 *
 * @param  address      address
 * @param  size         size (bytes)
 * @param  invert       inversion mask of the pattern
 */
//==============================================================================
static void sdram_fill_random(void *address, size_t size, u32_t invert)
{
        volatile u32_t *mem = address;
        u32_t x = SDRAM_TEST_SEED;

        for (size_t i = 0; i < (size / sizeof(u32_t)); i++) {
                x = sdram_random(x);
                mem[i] = x ^ invert;
        }
}

//==============================================================================
/**
 * @brief  Verify pseudo-random pattern. Memory is read by 8-word bursts (LDM),
 *         interrupts are enabled.
 *
 * @param  address      address
 * @param  size         size (bytes), multiple of SDRAM_BENCH_BURST
 * @param  invert       inversion mask of the pattern
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int sdram_verify_random(void *address, size_t size, u32_t invert)
{
        const u32_t *mem = address;
        u32_t x = SDRAM_TEST_SEED;
        u32_t buf[SDRAM_BENCH_BURST / sizeof(u32_t)];

        for (size_t n = 0; n < (size / SDRAM_BENCH_BURST); n++) {

                u32_t *dst = buf;

                __asm volatile("ldmia %[src]!, {r3, r4, r5, r6, r8, r9, r10, r12} \n"
                               "stmia %[dst], {r3, r4, r5, r6, r8, r9, r10, r12}  \n"
                               : [src] "+r"(mem)
                               : [dst] "r"(dst)
                               : "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r12", "memory");

                for (size_t i = 0; i < ARRAY_SIZE(buf); i++) {
                        x = sdram_random(x);
                        if (buf[i] != (x ^ invert)) {
                                dev_dbg2(0, 0, "error @ %p: %08lXh != %08lXh", mem - ARRAY_SIZE(buf) + i, buf[i],
                                        x ^ invert);
                                return EFAULT;
                        }
                }
        }

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Function test RAM memory. Memory content is lost.
 *
 * @param  address      address
 * @param  size         size (bytes)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int sdram_test(void *address, size_t size)
{
        dev_dbg2(0, 0, "memory test @ %p of %u bytes", address, size);

        int err = sdram_test_lines(address, size);

        if (not err) {
                err = sdram_test_byte_lanes(address);
        }

        if (not err) {
                /* data retention: memory is not accessed, only refreshed */
                sdram_fill_random(address, size, 0);
                sys_sleep_ms(SDRAM_TEST_RETENTION_MS);
                err = sdram_verify_random(address, size, 0);
        }

        if (not err) {
                sdram_fill_random(address, size, 0xFFFFFFFF);
                err = sdram_verify_random(address, size, 0xFFFFFFFF);
        }

        if (not err) {
                dev_dbg2(0, 0, "memory test success.");
                sdram_benchmark(address, size);
        } else {
                dev_dbg2(0, 0, "memory test failed.");
        }

        return err;
}
#endif

//==============================================================================
/**
 * @brief  Wait while the SDRAM is busy.
 */
//==============================================================================
static void sdram_wait_while_busy(void)
{
#if defined(ARCH_stm32f1)
#error No FMC implementation!
#elif defined(ARCH_stm32f4)
        while (FMC_BANK_5_6->SDSR & FMC_SDSR_BUSY)
                ;
#elif defined(ARCH_stm32f7)
        sys_sleep_ms(2);
#elif defined(ARCH_stm32h7)
        sys_sleep_ms(2);
#endif
}

//==============================================================================
/**
 * @brief  Get FMC kernel clock frequency.
 *
 * @return Frequency in Hz.
 */
//==============================================================================
static u32_t sdram_get_fmc_frequency(void)
{
        u32_t fmc_freq = 0;

#if defined(ARCH_stm32f1)
#elif defined(ARCH_stm32f4)
        LL_RCC_ClocksTypeDef freq;
        LL_RCC_GetSystemClocksFreq(&freq);
        fmc_freq = freq.HCLK_Frequency;
#elif defined(ARCH_stm32f7)
        LL_RCC_ClocksTypeDef freq;
        LL_RCC_GetSystemClocksFreq(&freq);
        fmc_freq = freq.HCLK_Frequency;
#elif defined(ARCH_stm32h7)
        fmc_freq = LL_RCC_GetFMCClockFreq(LL_RCC_FMC_CLKSOURCE);
#endif

        return fmc_freq;
}

//==============================================================================
/**
 * @brief  Calcualte SDRAM refresh rate.
 *
 * @return Calculated refresh rate.
 */
//==============================================================================
static u32_t sdram_calculate_refresh_rate(void)
{
        u32_t fmc_freq = sdram_get_fmc_frequency();

        i32_t NR = max((2 << (__EMI_SDRAM_1_NR__ + 10)), (2 << (__EMI_SDRAM_2_NR__ + 10)));
        i32_t MHz = fmc_freq / __EMI_SDRAM_SDCLK__ / 1000000;
        i32_t SRR = (((__EMI_SDRAM_REFRESH_RATE_MS__ * 1000) / NR) * MHz) - 20;

        if (SRR < 41) {
                dev_dbg2(0, 0, "incorrect SDRAM refresh rate.");
                SRR = 41;
        }

        return SRR;
}

//==============================================================================
/**
 * @brief Function initialize SDRAM controller.
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int sdram_init(void)
{
        FMC_ENABLE();

        /*
         * Timings are calculated for the configured frequency. A higher real
         * frequency means that delays are too short.
         */
        u32_t sdclk = sdram_get_fmc_frequency() / __EMI_SDRAM_SDCLK__;
        if (sdclk > (__EMI_SDRAM_FREQ_MHZ__ * 1000000UL)) {
                dev_dbg2(0, 0, "SDRAM clock %lu Hz is higher than configured %u MHz!", sdclk, __EMI_SDRAM_FREQ_MHZ__);
        }

        /*
         * 1. Program the memory device features into the FMC_SDCRx register.
         *    The SDRAM clock frequency, RBURST and RPIPE must be programmed
         *    in the FMC_SDCR1 register.
         */
        FMC_BANK_5_6->SDCR[0] = ((__EMI_SDRAM_RPIPE__ & 3) << FMC_SDCRx_RPIPE_Pos) |
                ((__EMI_SDRAM_RBURST__ & 1) << FMC_SDCRx_RBURST_Pos) |
                ((__EMI_SDRAM_SDCLK__ & 3) << FMC_SDCRx_SDCLK_Pos) | ((__EMI_SDRAM_CAS__ & 3) << FMC_SDCRx_CAS_Pos) |
                ((__EMI_SDRAM_1_NB__ & 1) << FMC_SDCRx_NB_Pos) | ((__EMI_SDRAM_1_MWID__ & 3) << FMC_SDCRx_MWID_Pos) |
                ((__EMI_SDRAM_1_NR__ & 3) << FMC_SDCRx_NR_Pos) | ((__EMI_SDRAM_1_NC__ & 3) << FMC_SDCRx_NC_Pos);

        FMC_BANK_5_6->SDCR[1] = ((__EMI_SDRAM_RPIPE__ & 3) << FMC_SDCRx_RPIPE_Pos) |
                ((__EMI_SDRAM_RBURST__ & 1) << FMC_SDCRx_RBURST_Pos) |
                ((__EMI_SDRAM_SDCLK__ & 3) << FMC_SDCRx_SDCLK_Pos) | ((__EMI_SDRAM_CAS__ & 3) << FMC_SDCRx_CAS_Pos) |
                ((__EMI_SDRAM_2_NB__ & 1) << FMC_SDCRx_NB_Pos) | ((__EMI_SDRAM_2_MWID__ & 3) << FMC_SDCRx_MWID_Pos) |
                ((__EMI_SDRAM_2_NR__ & 3) << FMC_SDCRx_NR_Pos) | ((__EMI_SDRAM_2_NC__ & 3) << FMC_SDCRx_NC_Pos);

        /*
         * 2. Program the memory device timing into the FMC_SDTRx register.
         *    The TRP and TRC timings must be programmed in the FMC_SDTR1
         *    register.
         *
         */
        FMC_BANK_5_6->SDTR[0] = (((SDRAM_1_TRCD - 1) & 0xF) << FMC_SDTRx_TRCD_Pos) |
                (((SDRAM_TRP - 1) & 0xF) << FMC_SDTRx_TRP_Pos) | (((SDRAM_TWR - 1) & 0xF) << FMC_SDTRx_TWR_Pos) |
                (((SDRAM_TRC - 1) & 0xF) << FMC_SDTRx_TRC_Pos) | (((SDRAM_TRAS - 1) & 0xF) << FMC_SDTRx_TRAS_Pos) |
                (((SDRAM_TXSR - 1) & 0xF) << FMC_SDTRx_TXSR_Pos) | (((SDRAM_TMRD - 1) & 0xF) << FMC_SDTRx_TMRD_Pos);

        FMC_BANK_5_6->SDTR[1] = (((SDRAM_2_TRCD - 1) & 0xF) << FMC_SDTRx_TRCD_Pos) |
                (((SDRAM_TRP - 1) & 0xF) << FMC_SDTRx_TRP_Pos) | (((SDRAM_TWR - 1) & 0xF) << FMC_SDTRx_TWR_Pos) |
                (((SDRAM_TRC - 1) & 0xF) << FMC_SDTRx_TRC_Pos) | (((SDRAM_TRAS - 1) & 0xF) << FMC_SDTRx_TRAS_Pos) |
                (((SDRAM_TXSR - 1) & 0xF) << FMC_SDTRx_TXSR_Pos) | (((SDRAM_TMRD - 1) & 0xF) << FMC_SDTRx_TMRD_Pos);

        /* From this point software is sending commands to SDRAM chips.*/
        /*
         * 3. Set MODE bits to '001' and configure the Target Bank bits
         *    (CTB1 and/or CTB2) in the FMC_SDCMR register to start delivering
         *    the clock to the memory (SDCKE is driven high).
         */
        sdram_wait_while_busy();

        FMC_BANK_5_6->SDCMR = (SDRAM_CMD__CLK_CFG_EN) | (__EMI_SDRAM_1_ENABLE__ * FMC_SDCMR_CTB1) |
                (__EMI_SDRAM_2_ENABLE__ * FMC_SDCMR_CTB2) | (1 << FMC_SDCMR_NRFS_Pos);

        /*
         * 4. Wait during the prescribed delay period. Typical delay is around
         *    100 μs (refer to the SDRAM datasheet for the required delay after
         *    power-up).
         */
        sys_sleep_ms(1);

        /*
         * 5. Set MODE bits to ‘010’ and configure the Target Bank bits
         *    (CTB1 and/or CTB2) in the FMC_SDCMR register to issue
         *    a “Precharge All” command.
         */
        sdram_wait_while_busy();

        FMC_BANK_5_6->SDCMR = (SDRAM_CMD__PRECHARGE_ALL) | (__EMI_SDRAM_1_ENABLE__ * FMC_SDCMR_CTB1) |
                (__EMI_SDRAM_2_ENABLE__ * FMC_SDCMR_CTB2) | (1 << FMC_SDCMR_NRFS_Pos);

        /*
         * 6. Set MODE bits to ‘011’, and configure the Target Bank bits
         *    (CTB1 and/or CTB2) as well as the number of consecutive Auto-refresh
         *    commands (NRFS) in the FMC_SDCMR register. Refer to the SDRAM
         *    datasheet for the number of Auto-refresh commands that should be
         *    issued. Typical number is 8.
         */
        sdram_wait_while_busy();

        FMC_BANK_5_6->SDCMR = (SDRAM_CMD__AUTOREFRESH) | (__EMI_SDRAM_1_ENABLE__ * FMC_SDCMR_CTB1) |
                (__EMI_SDRAM_2_ENABLE__ * FMC_SDCMR_CTB2) | (((__EMI_SDRAM_NRFS__ - 1) & 0xF) << FMC_SDCMR_NRFS_Pos);

        /*
         * 7. Configure the MRD field according to your SDRAM device, set the
         *    MODE bits to '100', and configure the Target Bank bits
         *    (CTB1 and/or CTB2) in the FMC_SDCMR register to issue a
         *    "Load Mode Register" command in order to program the SDRAM. In
         *    particular:
         *    a) The CAS latency must be selected following configured value in
         *       FMC_SDCR1/2 registers
         *    b) The Burst Length (BL) of 1 must be selected by configuring the
         *       M[2:0] bits to 000 in the mode register (refer to the SDRAM
         *       datasheet). If the Mode Register is not the same for both SDRAM
         *       banks, this step has to be repeated twice, once for each bank,
         *       and the Target Bank bits set accordingly.
         */
        sdram_wait_while_busy();

        u32_t MRD = SDRAM_MODE_REG_WRITE_BURST_MODE_SINGLE_LOCATION_ACCESS | SDRAM_MODE_REG_OPERATING_MODE_STANDARD |
                (__EMI_SDRAM_CAS__ << SDRAM_MODE_REG_CAS_LATENCY_Pos) | SDRAM_MODE_REG_BURST_TYPE_SEQUENTIAL |
                SDRAM_MODE_REG_BURST_LENGTH_1;

        FMC_BANK_5_6->SDCMR = (SDRAM_CMD__LOADMODEREG) | (__EMI_SDRAM_1_ENABLE__ * FMC_SDCMR_CTB1) |
                (__EMI_SDRAM_2_ENABLE__ * FMC_SDCMR_CTB2) | (1 << FMC_SDCMR_NRFS_Pos) | (MRD << FMC_SDCMR_MRD_Pos);

        /*
         * 8. Program the refresh rate in the FMC_SDRTR register. The refresh
         *    rate corresponds to the delay between refresh cycles. Its value
         *    must be adapted to SDRAM devices.
         */
        sdram_wait_while_busy();

        u32_t SRR = sdram_calculate_refresh_rate();
        FMC_BANK_5_6->SDRTR |= (SRR << FMC_SDRTR_COUNT_Pos);

        sdram_wait_while_busy();

        /*
         * Register memory regions in system.
         */
        int err1 = ESUCC;
        int err2 = ESUCC;

#if __EMI_SDRAM_1_ENABLE__ > 0
        u32_t mem_addr1 = SDRAM1_ADDRESS;

        size_t mem_size1 = (2 << (__EMI_SDRAM_1_NR__ + 10)) // Row bits (0 - 11 bits: A10)
                * (2 << (__EMI_SDRAM_1_NC__ + 7))           // Col bits (0 - 8 bits:  A7)
                * (2 << (__EMI_SDRAM_1_NB__))               // Banks (2 or 4)
                * (8 << (__EMI_SDRAM_1_MWID__))             // Bus width
                / (8);                                      // Bits per byte

#if __EMI_SDRAM_TEST_ENABLE__ == _YES_
        err1 = sdram_test((void *)mem_addr1, mem_size1);
#endif
        if (!err1) {
                err1 = register_heap_regions(mem_addr1, mem_size1, __EMI_SDRAM_1_REGIONS__);
        }
#endif

#if __EMI_SDRAM_2_ENABLE__ > 0
        u32_t mem_addr2 = SDRAM2_ADDRESS;

        size_t mem_size2 = (2 << (__EMI_SDRAM_2_NR__ + 10)) // Row bits (0 - 11 bits: A10)
                * (2 << (__EMI_SDRAM_2_NC__ + 7))           // Col bits (0 - 8 bits:  A7)
                * (2 << (__EMI_SDRAM_2_NB__))               // Banks (2 or 4)
                * (8 << (__EMI_SDRAM_2_MWID__))             // Bus width
                / (8);                                      // Bits per byte

#if __EMI_SDRAM_TEST_ENABLE__ == _YES_
        err2 = sdram_test((void *)mem_addr2, mem_size2);
#endif
        if (!err2) {
                err1 = register_heap_regions(mem_addr2, mem_size2, __EMI_SDRAM_2_REGIONS__);
        }
#endif

        return err1 ? err1 : (err2 ? err2 : ESUCC);
}

//==============================================================================
/**
 * @brief  Find region type in options.
 *
 * @param  opts         input options string
 * @param  token        input token (key)
 * @param  name         output region name
 * @param  name_len     buffer size of region name
 * @param  addr         region address
 * @param  size         region size
 *
 * @return NULL or next option.
 */
//==============================================================================
static const char *find_opt_region(const char *opts, const char *token, char *name, size_t name_len, u32_t *addr,
        u32_t *size)
{
        size_t len = 0;

        const char *v = sys_opt_get_str_ref(opts, token, &len);
        if (v) {
                sys_strlcpy(name, v, name_len);
                char *colon = strchr(name, ':');
                if (colon) {
                        *colon = '\0';
                }

                colon = sys_strtou32(++colon, 0, addr);

                sys_strtou32(++colon, 0, size);
        }

        return v + len;
}

//==============================================================================
/**
 * @brief  Function register selected number of memory regions in SDRAM.
 *
 * @param  addr         memory start address
 * @param  mem_size     memory size
 * @param  region       region array
 * @param  region_num   number of regions in array
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
static int register_heap_regions(u32_t mem_addr, size_t mem_size, const char *opts)
{
        int err = ESUCC;
        char name[64];
        u32_t reg_addr = 0;
        u32_t reg_size = 0;

        const char *REGION_TYPE[] = {"HEAP", "NOHEAP"};
        const u32_t REGION_FLAGS[] = {_MM_FLAG__DMA, _MM_FLAG__DMA | _MM_FLAG__NOHEAP};

        for (size_t i = 0; not err and (i < ARRAY_SIZE(REGION_TYPE)); i++) {

                const char *o = opts;
                const char *region_type = REGION_TYPE[i];
                u32_t region_flags = REGION_FLAGS[i];

                while (o) {
                        o = find_opt_region(o, region_type, name, sizeof(name), &reg_addr, &reg_size);
                        if (o) {
                                if (reg_addr < mem_addr) {
                                        dev_dbg2(0, 0, "invalid region address: %lXh", reg_addr);
                                        continue;

                                } else if (reg_addr >= (mem_addr + mem_size)) {
                                        dev_dbg2(0, 0, "invalid region address: %lXh", reg_addr);
                                        continue;

                                } else if ((mem_size - (reg_addr - mem_addr)) < reg_size) {
                                        dev_dbg2(0, 0, "too big region size, trimmed to memory size");
                                        reg_size = (mem_size - (reg_addr - mem_addr));
                                }

                                err = drv_mem_register_region(cast(void *, reg_addr), reg_size, region_flags, name);

                                if (err) {
                                        break;
                                }
                        }
                }
        }

        return err;
}
#endif

/*==============================================================================
  End of file
==============================================================================*/
