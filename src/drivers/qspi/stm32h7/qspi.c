/*==============================================================================
File    qspi.c

Author  Daniel Zorychta

Brief   Quad SPI

        Copyright (C) 2024 Daniel Zorychta <daniel.zorychta@gmail.com>

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

// #pragma GCC optimize("Og")

/*==============================================================================
  Include files
==============================================================================*/
#include "drivers/driver.h"
#include "../qspi_ioctl.h"
#include "stm32h7xx_hal_qspi.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define QSPI_BLOCKS        2

#define _QSPI_MODE_POLLING 0
#define _QSPI_MODE_IRQ     1
#define _QSPI_MODE_DMA     2

/*==============================================================================
  Local object types
==============================================================================*/
typedef struct {
        u8_t          major;
        u8_t          minor;
        QSPI_config_t config;
        bool          configured;
} QSPI_block_t;

typedef struct {
        QSPI_block_t      *block[QSPI_BLOCKS];
        kmtx_t            *mutex;
        ksem_t            *sem_xfer_complete;
        QSPI_HandleTypeDef hqspi;
        bool               memory_mapped_mode;
        bool               irq_yield;
} QSPI_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void  release_resources(QSPI_block_t *hdl);
static int   qspi_configure(QSPI_block_t *hdl, const QSPI_config_t *config);
static int   qspi_command(QSPI_block_t *hdl, const QSPI_command_t *command);
static int   qspi_enter_memory_mapped_mode(QSPI_block_t *hdl,
          const QSPI_command_t                          *command);
static int   qspi_exit_memory_mapped_mode(QSPI_block_t *hdl);
static u32_t get_memory_size(QSPI_block_t *hdl);
static int   command_to_hal(const QSPI_command_t *command, QSPI_CommandTypeDef *hal_cmd);
static int   wait_for_memory_to_be_ready(QSPI_block_t *hdl, u32_t timeout);
//static int   register_DMA_channel(QSPI_block_t *hdl);

/*==============================================================================
  Local object
==============================================================================*/
MODULE_NAME(QSPI);
static QSPI_t QSPI;

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
API_MOD_INIT(QSPI, void **device_handle, u8_t major, u8_t minor, const void *config)
{
        int err = EFAULT;

        if (major != 0) {
                return ENODEV;
        }

        if (minor >= QSPI_BLOCKS) {
                return ENODEV;
        }

        err = sys_zalloc(sizeof(QSPI_block_t), device_handle);
        if (err) {
                return err;
        }

        QSPI_block_t *hdl = *device_handle;
        hdl->major        = major;
        hdl->minor        = minor;

        if (not QSPI.mutex) {
                err = sys_mutex_create(KMTX_TYPE_RECURSIVE, &QSPI.mutex);
                if (err) {
                        release_resources(hdl);
                        *device_handle = NULL;
                        return err;
                }
        }

        if (not QSPI.sem_xfer_complete) {
                err = sys_semaphore_create(1, 0, &QSPI.sem_xfer_complete);
                if (err) {
                        release_resources(hdl);
                        *device_handle = NULL;
                        return err;
                }
        }

        if (config) {
                err = qspi_configure(hdl, config);
                if (err) {
                        release_resources(hdl);
                        *device_handle = NULL;
                        return err;
                }
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
API_MOD_RELEASE(QSPI, void *device_handle)
{
        release_resources(device_handle);

        return 0;
}

//==============================================================================
/**
 * @brief Open device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[in ]           flags                  file operation flags (O_RDONLY, O_WRONLY,
 * O_RDWR)
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_OPEN(QSPI, void *device_handle, u32_t flags)
{
        UNUSED_ARG2(device_handle, flags);

        return ESUCC;
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
API_MOD_CLOSE(QSPI, void *device_handle, bool force)
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
 * @param[out]          *wrctr                  number of written bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_WRITE(QSPI, void *device_handle, const u8_t *src, size_t count, fpos_t *fpos,
        size_t *wrctr, struct vfs_fattr fattr)
{
        QSPI_block_t *hdl = device_handle;

        if (not hdl->configured) {
                return EAGAIN;
        }

        size_t memory_size = get_memory_size(hdl);

        *wrctr = 0;

        if (*fpos > memory_size) {
                dev_dbg(hdl, "write out of range %08Xh", (u32_t)*fpos);
                return ESPIPE;
        }

        count = min(count, (memory_size - *fpos));

        int err = sys_mutex_lock(QSPI.mutex, fattr.non_blocking_wr ? 0 : _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        uint32_t addr      = *fpos;
        size_t   page_size = hdl->config.page_size;

        while (!err && count) {
                QSPI_command_t cmd;

                size_t pbleft = (((addr / page_size) + 1) * page_size) - addr;
                size_t wrsz   = min(count, pbleft);

                /*
                 * Write enable.
                 */
                cmd           = *hdl->config.write_enable_command;
                cmd.data_size = 0;
                cmd.data      = NULL;

                err = qspi_command(hdl, &cmd);
                if (err) {
                        break;
                }

                /*
                 * Page program command and data transfer.
                 */
                cmd           = *hdl->config.page_program_command;
                cmd.address   = addr;
                cmd.data      = const_cast(src);
                cmd.data_size = wrsz;
                cmd.direction = QSPI_DIRECTION__WRITE;

                err = qspi_command(hdl, &cmd);
                if (err) {
                        break;
                }

                err = wait_for_memory_to_be_ready(hdl, __QSPI_CFG_TIMEOUT__);
                if (err) {
                        break;
                }

                *wrctr += wrsz;
                src += wrsz;
                addr += wrsz;
                count -= wrsz;
        }

        sys_mutex_unlock(QSPI.mutex);

        return err;
}

//==============================================================================
/**
 * @brief Read data from device.
 *
 * @param[in ]          *device_handle          device allocated memory
 * @param[out]          *dst                    data destination
 * @param[in ]           count                  number of bytes to read
 * @param[in ][out]     *fpos                   file position
 * @param[out]          *rdctr                  number of read bytes
 * @param[in ]           fattr                  file attributes
 *
 * @return One of errno value (errno.h).
 */
//==============================================================================
API_MOD_READ(QSPI, void *device_handle, u8_t *dst, size_t count, fpos_t *fpos,
        size_t *rdctr, struct vfs_fattr fattr)
{
        QSPI_block_t *hdl = device_handle;

        if (not hdl->configured) {
                return EAGAIN;
        }

        *rdctr = 0;

        size_t memory_size = get_memory_size(hdl);

        if (*fpos > memory_size) {
                dev_dbg(hdl, "read out of range %08Xh", (u32_t)*fpos);
                return ESPIPE;
        }

        count = min(count, (memory_size - *fpos));

        int err = sys_mutex_lock(QSPI.mutex, fattr.non_blocking_rd ? 0 : _WAIT_FOREVER_MS);
        if (err) {
                return err;
        }

        QSPI_command_t cmd = *hdl->config.read_command;
        cmd.address        = *fpos;
        cmd.data           = dst;
        cmd.data_size      = count;
        cmd.direction      = QSPI_DIRECTION__READ;

        err = qspi_command(hdl, &cmd);
        if (not err) {
                *rdctr = count;
        }

        sys_mutex_unlock(QSPI.mutex);

        return err;
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
API_MOD_IOCTL(QSPI, void *device_handle, int request, void *arg)
{
        QSPI_block_t *hdl = device_handle;

        int err = EBADRQC;

        switch (request) {
        case IOCTL_QSPI__CONFIGURE:
                err = sys_mutex_lock(QSPI.mutex, _WAIT_FOREVER_MS);
                if (not err) {
                        err = qspi_configure(hdl, arg);
                        sys_mutex_unlock(QSPI.mutex);
                }
                break;

        case IOCTL_QSPI__COMMAND:
                err = sys_mutex_lock(QSPI.mutex, _WAIT_FOREVER_MS);
                if (not err) {
                        err = qspi_command(hdl, arg);
                        sys_mutex_unlock(QSPI.mutex);
                }
                break;

        case IOCTL_QSPI__ENTER_MEMORY_MAPPED_MODE:
                err = sys_mutex_lock(QSPI.mutex, _WAIT_FOREVER_MS);
                if (not err) {
                        err = qspi_enter_memory_mapped_mode(hdl, arg);
                        sys_mutex_unlock(QSPI.mutex);
                }
                break;

        case IOCTL_QSPI__EXIT_MEMORY_MAPPED_MODE:
                err = sys_mutex_lock(QSPI.mutex, _WAIT_FOREVER_MS);
                if (not err) {
                        err = qspi_exit_memory_mapped_mode(hdl);
                        sys_mutex_unlock(QSPI.mutex);
                }
                break;

        case IOCTL_QSPI__WAIT_FOR_MEMORY_TO_BE_READY:
                if (arg) {
                        err = sys_mutex_lock(QSPI.mutex, _WAIT_FOREVER_MS);
                        if (not err) {
                                err = wait_for_memory_to_be_ready(hdl,
                                        *cast(u32_t *, arg));
                                sys_mutex_unlock(QSPI.mutex);
                        }
                } else {
                        err = EINVAL;
                }
                break;
        }

        return err;
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
API_MOD_FLUSH(QSPI, void *device_handle)
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
API_MOD_STAT(QSPI, void *device_handle, struct vfs_dev_stat *device_stat)
{
        QSPI_block_t *hdl = device_handle;

        device_stat->st_size = hdl->configured ? get_memory_size(hdl) : 0;

        return ESUCC;
}

//==============================================================================
/**
 * @brief  Release driver resources.
 *
 * @param  hdl          device handle.
 */
//==============================================================================
static void release_resources(QSPI_block_t *hdl)
{
        u8_t minor = hdl->minor;

        sys_free(cast(void **, &hdl));

        QSPI.block[minor] = NULL;

        bool used = false;

        for (size_t i = 0; i < QSPI_BLOCKS; i++) {
                if (QSPI.block[i]) {
                        used = true;
                        break;
                }
        }

        if (not used) {
                //if (QSPI.hqspi.hdma) {
                //        _DMA_DDI_release(QSPI.hqspi.hdma->dmad);
                //        sys_free(cast(void **, QSPI.hqspi.hdma));
                //}

                if (QSPI.hqspi.Instance) {
                        HAL_QSPI_DeInit(&QSPI.hqspi);
                }

                if (QSPI.mutex) {
                        sys_mutex_destroy(QSPI.mutex);
                }

                if (QSPI.sem_xfer_complete) {
                        sys_semaphore_destroy(QSPI.sem_xfer_complete);
                }

                memset(&QSPI, 0, sizeof(QSPI));
        }
}

//==============================================================================
/**
 * @brief  Check if command is correct.
 *
 * @param  command      command to check
 *
 * @return True if command is valid, otherwise false.
 */
//==============================================================================
static bool is_command_config_valid(const QSPI_command_t *command)
{
        if (not command) {
                return false;
        }

        if (command->instruction_mode > QSPI_MODE__QUAD) {
                return false;
        }

        if (command->address_mode > QSPI_MODE__QUAD) {
                return false;
        }

        if (command->data_mode > QSPI_MODE__QUAD) {
                return false;
        }

        if (command->alternate_byte_mode > QSPI_MODE__QUAD) {
                return false;
        }

        if (command->ddr_hold_cycle > QSPI_DDR_HOLD_CYCLE__HALF_CLK_DELAY) {
                return false;
        }

        if ((command->data_mode == QSPI_MODE__NONE) and
                (command->data_size or command->data)) {
                return false;
        }

        if (command->data_size and not command->data) {
                return false;
        }

        if (not IS_QSPI_DUMMY_CYCLES(command->dummy_cycles)) {
                return false;
        }

        if (command->address_size > QSPI_SIZE__32_BITS) {
                return false;
        }

        if (command->direction > QSPI_DIRECTION__WRITE) {
                return false;
        }

        if (command->alternate_bytes_size > QSPI_SIZE__32_BITS) {
                return false;
        }

        return true;
}

//==============================================================================
/**
 * @brief  Gets memory size.
 *
 * @param  hdl          device handle.
 *
 * @return Memory size.
 */
//==============================================================================
static u32_t get_memory_size(QSPI_block_t *hdl)
{
        return (1 << hdl->config.flash_size_bits) * (hdl->config.dual_flash ? 2 : 1);
}

//==============================================================================
/**
 * @brief  Wait for memory to be ready.
 *
 * @param  hdl          device handle
 * @param  timeout      timeout in milliseconds
 *
 * @return One of errno value.
 */
//==============================================================================
static int wait_for_memory_to_be_ready(QSPI_block_t *hdl, u32_t timeout)
{
        int err = ETIME;

        static const uint32_t hal_match_size[] = {
                [QSPI_SIZE__8_BITS]  = 1,
                [QSPI_SIZE__16_BITS] = 2,
                [QSPI_SIZE__24_BITS] = 3,
                [QSPI_SIZE__32_BITS] = 4,
        };

        QSPI_AutoPollingTypeDef auto_polling_cfg;
        auto_polling_cfg.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;
        auto_polling_cfg.Interval        = __QSPI_CFG_POLLING_INTERVAL_CLKS__;
        auto_polling_cfg.Mask            = hdl->config.write_in_progress_mask;
        auto_polling_cfg.Match           = hdl->config.write_in_progress_match;
        auto_polling_cfg.MatchMode       = QSPI_MATCH_MODE_AND;
        auto_polling_cfg.StatusBytesSize = hal_match_size
                [hdl->config.write_in_progress_register_size];

        QSPI_CommandTypeDef hal_cmd;
        command_to_hal(hdl->config.read_status_register_command, &hal_cmd);

#if (__QSPI_CFG_MODE__ == _QSPI_MODE_POLLING)
        err = HAL_QSPI_AutoPolling(&QSPI.hqspi, &hal_cmd, &auto_polling_cfg, timeout);

#elif (__QSPI_CFG_MODE__ == _QSPI_MODE_IRQ) || (__QSPI_CFG_MODE__ == _QSPI_MODE_DMA)
        err = HAL_QSPI_AutoPolling_IT(&QSPI.hqspi, &hal_cmd, &auto_polling_cfg);
        if (not err) {
                err = sys_semaphore_wait(QSPI.sem_xfer_complete, timeout);
                if (err) {
                        HAL_QSPI_Abort(&QSPI.hqspi);
                }
        }
#endif

        return err;
}

//==============================================================================
/**
 * @brief  Register DMA channel.
 *
 * @param  hdl          device handle
 *
 * @return One of errno value.
 */
//==============================================================================
//static int register_DMA_channel(QSPI_block_t *hdl)
//{
//        UNUSED_ARG1(hdl);
//
//        QSPI_DMA_HandleTypeDef *dmah;
//
//        int err = sys_zalloc(sizeof(*dmah), cast(void **, &dmah));
//        if (err) {
//                return err;
//        }
//
//        dmah->dmad = _DMA_DDI_reserve(_DMA_DDI_DMA2, 7);
//        if (!dmah->dmad) {
//                sys_free(cast(void **, &dmah));
//                return EBUSY;
//        }
//
//        // clang-format off
//        dmah->Config.user_ctx                     = &QSPI.hqspi;
//        dmah->Config.cb_finish                    = NULL;
//        dmah->Config.cb_half                      = NULL;
//        dmah->Config.cb_next                      = NULL;
//        dmah->Config.data_number                  = 0;
//        dmah->Config.peripheral_address           = cast(u32_t, &QSPI.hqspi.Instance->DR);
//        dmah->Config.memory_address[0]            = 0;
//        dmah->Config.memory_address[1]            = 0;
//        dmah->Config.IRQ_priority                 = __QSPI_CFG_IRQ_PRIORITY__;
//        dmah->Config.channel                      = 3;
//        dmah->Config.release                      = false;
//        dmah->Config.mode                         = _DMA_DDI_MODE_FIFO;
//        dmah->Config.fifo_threshold               = _DMA_DDI_FIFO_THRESHOLD_FULL;
//        dmah->Config.memory_burst                 = _DMA_DDI_MEMORY_BURST_SINGLE_TRANSFER;
//        dmah->Config.peripheral_burst             = _DMA_DDI_PERIPHERAL_BURST_SINGLE_TRANSFER;
//        dmah->Config.double_buffer_mode           = _DMA_DDI_DOUBLE_BUFFER_MODE_DISABLED;
//        dmah->Config.priority_level               = _DMA_DDI_PRIORITY_LEVEL_LOW;
//        dmah->Config.peripheral_increment_offset  = _DMA_DDI_PERIPHERAL_INCREMENT_OFFSET_ACCORDING_TO_PERIPHERAL_SIZE;
//        dmah->Config.memory_data_size             = _DMA_DDI_MEMORY_DATA_SIZE_BYTE;
//        dmah->Config.peripheral_data_size         = _DMA_DDI_PERIPHERAL_DATA_SIZE_BYTE;
//        dmah->Config.memory_address_increment     = _DMA_DDI_MEMORY_ADDRESS_POINTER_INCREMENTED;
//        dmah->Config.peripheral_address_increment = _DMA_DDI_PERIPHERAL_ADDRESS_POINTER_IS_FIXED;
//        dmah->Config.circular_mode                = _DMA_DDI_CIRCULAR_MODE_DISABLED;
//        dmah->Config.transfer_direction           = _DMA_DDI_TRANSFER_DIRECTION_MEMORY_TO_PERIPHERAL;
//        dmah->Config.flow_controller              = _DMA_DDI_FLOW_CONTROLLER_DMA;
//        // clang-format on
//
//        dmah->Parent    = &QSPI.hqspi;
//        QSPI.hqspi.hdma = dmah;
//
//        return err;
//}

//==============================================================================
/**
 * @brief  Convert command to HAL-version command.
 *
 * @param  command              generic command.
 * @param  hal_cmd              HAL command
 *
 * @return One of errno value.
 */
//==============================================================================
static int command_to_hal(const QSPI_command_t *command, QSPI_CommandTypeDef *hal_cmd)
{
        if (not is_command_config_valid(command) or not hal_cmd) {
                return EINVAL;
        }

        static const uint32_t hal_address_mode[] = {
                [QSPI_MODE__NONE]   = QSPI_ADDRESS_NONE,
                [QSPI_MODE__SINGLE] = QSPI_ADDRESS_1_LINE,
                [QSPI_MODE__DOUBLE] = QSPI_ADDRESS_2_LINES,
                [QSPI_MODE__QUAD]   = QSPI_ADDRESS_4_LINES,
        };

        static const uint32_t hal_alt_byte_mode[] = {
                [QSPI_MODE__NONE]   = QSPI_ALTERNATE_BYTES_NONE,
                [QSPI_MODE__SINGLE] = QSPI_ALTERNATE_BYTES_1_LINE,
                [QSPI_MODE__DOUBLE] = QSPI_ALTERNATE_BYTES_2_LINES,
                [QSPI_MODE__QUAD]   = QSPI_ALTERNATE_BYTES_4_LINES,
        };

        static const uint32_t hal_data_mode[] = {
                [QSPI_MODE__NONE]   = QSPI_DATA_NONE,
                [QSPI_MODE__SINGLE] = QSPI_DATA_1_LINE,
                [QSPI_MODE__DOUBLE] = QSPI_DATA_2_LINES,
                [QSPI_MODE__QUAD]   = QSPI_DATA_4_LINES,
        };

        static const uint32_t hal_instruction_mode[] = {
                [QSPI_MODE__NONE]   = QSPI_INSTRUCTION_NONE,
                [QSPI_MODE__SINGLE] = QSPI_INSTRUCTION_1_LINE,
                [QSPI_MODE__DOUBLE] = QSPI_INSTRUCTION_2_LINES,
                [QSPI_MODE__QUAD]   = QSPI_INSTRUCTION_4_LINES,
        };

        static const uint32_t hal_address_size[] = {
                [QSPI_SIZE__8_BITS]  = QSPI_ADDRESS_8_BITS,
                [QSPI_SIZE__16_BITS] = QSPI_ADDRESS_16_BITS,
                [QSPI_SIZE__24_BITS] = QSPI_ADDRESS_24_BITS,
                [QSPI_SIZE__32_BITS] = QSPI_ADDRESS_32_BITS,
        };

        static const uint32_t hal_alt_bytes_size[] = {
                [QSPI_SIZE__8_BITS]  = QSPI_ALTERNATE_BYTES_8_BITS,
                [QSPI_SIZE__16_BITS] = QSPI_ALTERNATE_BYTES_16_BITS,
                [QSPI_SIZE__24_BITS] = QSPI_ALTERNATE_BYTES_24_BITS,
                [QSPI_SIZE__32_BITS] = QSPI_ALTERNATE_BYTES_32_BITS,
        };

        static const uint32_t hal_ddr_hhc[] = {
                [QSPI_DDR_HOLD_CYCLE__ANALOG_DELAY]   = QSPI_DDR_HHC_ANALOG_DELAY,
                [QSPI_DDR_HOLD_CYCLE__HALF_CLK_DELAY] = QSPI_DDR_HHC_HALF_CLK_DELAY,
        };

        static const uint32_t hal_ddr_mode[] = {
                [false] = QSPI_DDR_MODE_DISABLE,
                [true]  = QSPI_DDR_MODE_ENABLE,
        };

        static const uint32_t hal_SIOO_mode[] = {
                [false] = QSPI_SIOO_INST_EVERY_CMD,
                [true]  = QSPI_SIOO_INST_ONLY_FIRST_CMD,
        };

        hal_cmd->Address            = command->address;
        hal_cmd->AddressMode        = hal_address_mode[command->address_mode];
        hal_cmd->AddressSize        = hal_address_size[command->address_size];
        hal_cmd->AlternateByteMode  = hal_alt_byte_mode[command->alternate_byte_mode];
        hal_cmd->AlternateBytes     = command->alternate_bytes;
        hal_cmd->AlternateBytesSize = hal_alt_bytes_size[command->alternate_bytes_size];
        hal_cmd->DataMode           = hal_data_mode[command->data_mode];
        hal_cmd->DdrHoldHalfCycle   = hal_ddr_hhc[command->ddr_hold_cycle];
        hal_cmd->DdrMode            = hal_ddr_mode[command->ddr];
        hal_cmd->DummyCycles        = command->dummy_cycles;
        hal_cmd->Instruction        = command->instruction;
        hal_cmd->InstructionMode    = hal_instruction_mode[command->instruction_mode];
        hal_cmd->NbData             = command->data_size;
        hal_cmd->SIOOMode           = hal_SIOO_mode[command->instruction_only_once];

        return 0;
}

//==============================================================================
/**
 * @brief  Configure QSPI.
 *
 * @param  hdl          device handle
 * @param  config       configuration
 *
 * @return One of errno value.
 */
//==============================================================================
static int qspi_configure(QSPI_block_t *hdl, const QSPI_config_t *config)
{
        if ((config->clock_prescaller < 1) or (config->clock_prescaller > 256)) {
                dev_dbg(hdl, "prescaller out of range");
                return EINVAL;
        }

        if (config->sample_shifting > QSPI_SAMPLE_SHIFTING__HALF_CYCLE) {
                dev_dbg(hdl, "invalid sample shifting value");
                return EINVAL;
        }

        if ((config->flash_size_bits < 1) or (config->flash_size_bits > 32)) {
                dev_dbg(hdl, "invalid flash size");
                return EINVAL;
        }

        if ((config->chip_select_high_time < 1) or (config->chip_select_high_time > 8)) {
                dev_dbg(hdl, "invalid chip select high time clocks");
                return EINVAL;
        }

        if (config->clock_mode > QSPI_CLOCK_MODE__3) {
                dev_dbg(hdl, "invalid clock mode");
                return EINVAL;
        }

        if (config->page_size == 0) {
                dev_dbg(hdl, "invalid page size");
                return EINVAL;
        }

        if (not is_command_config_valid(config->read_command)) {
                dev_dbg(hdl, "invalid configuration of read command");
                return EINVAL;
        }

        if (config->write_in_progress_register_size > QSPI_SIZE__32_BITS) {
                dev_dbg(hdl, "invalid size of WIP register");
                return EINVAL;
        }

        if (not is_command_config_valid(config->page_program_command)) {
                dev_dbg(hdl, "invalid configuration of page program command");
                return EINVAL;
        }

        if (not is_command_config_valid(config->write_enable_command)) {
                dev_dbg(hdl, "invalid configuration of write enable command");
                return EINVAL;
        }

        if (not is_command_config_valid(config->read_status_register_command)) {
                dev_dbg(hdl, "invalid configuration of read status register command");
                return EINVAL;
        }

        if (QSPI.memory_mapped_mode) {
                dev_dbg(hdl, "configuration skipped - memory mapped mode enabled");
                return EBUSY;
        }

        if ((hdl->minor > 0) and QSPI.block[0] and QSPI.block[0]->configured and
                QSPI.block[0]->config.dual_flash) {
                dev_dbg(hdl, "unable to initialize block 1: dual flash mode enabled");
                return EPERM;
        }

        static const uint32_t sample_shift[] = {
                [QSPI_SAMPLE_SHIFTING__NONE]       = QSPI_SAMPLE_SHIFTING_NONE,
                [QSPI_SAMPLE_SHIFTING__HALF_CYCLE] = QSPI_SAMPLE_SHIFTING_HALFCYCLE,
        };

        static const uint32_t clock_mode[] = {
                [QSPI_CLOCK_MODE__0] = QSPI_CLOCK_MODE_0,
                [QSPI_CLOCK_MODE__3] = QSPI_CLOCK_MODE_3,
        };

        static const uint32_t dual_flash[] = {
                [false] = QSPI_DUALFLASH_DISABLE,
                [true]  = QSPI_DUALFLASH_ENABLE,
        };

        int err = 0;

        if (hdl->configured) {
                HAL_QSPI_DeInit(&QSPI.hqspi);
        }

        hdl->configured = false;

        if (not QSPI.hqspi.Instance) {
                QSPI.hqspi.Instance            = QUADSPI;
                QSPI.hqspi.Init.ClockPrescaler = config->clock_prescaller - 1;
                QSPI.hqspi.Init.FifoThreshold  = 4;
                QSPI.hqspi.Init.SampleShifting = sample_shift[config->sample_shifting];
                QSPI.hqspi.Init.FlashSize      = config->flash_size_bits - 1;
                QSPI.hqspi.Init.ClockMode      = clock_mode[config->clock_mode];
                QSPI.hqspi.Init.FlashID        = QSPI_FLASH_ID_1;
                QSPI.hqspi.Init.DualFlash      = dual_flash[config->dual_flash];
                QSPI.hqspi.Init.ChipSelectHighTime = config->chip_select_high_time - 1;

                err = HAL_QSPI_Init(&QSPI.hqspi);
        }

#if (__QSPI_CFG_MODE__ == _QSPI_MODE_DMA)
        //if (not QSPI.hqspi.hdma) {
        //        register_DMA_channel(hdl);
        //}
#endif

        if (not err) {
                hdl->config     = *config;
                hdl->configured = true;

                dev_dbg(hdl, "memory size: %lu bytes",
                        (1 << config->flash_size_bits) * (config->dual_flash ? 2 : 1));

                if (config->dual_flash) {
                        dev_dbg(hdl, "enabled dual flash mode");
                }

#if (__QSPI_CFG_MODE__ == _QSPI_MODE_POLLING)
                dev_dbg(hdl, "using polling mode");

#elif (__QSPI_CFG_MODE__ == _QSPI_MODE_IRQ)
                dev_dbg(hdl, "using IRQ mode");

#elif (__QSPI_CFG_MODE__ == _QSPI_MODE_DMA)
                //if (QSPI.hqspi.hdma) {
                //        dev_dbg(hdl, "using DMA mode");
                //} else {
                        dev_dbg(hdl, "DMA channel busy - using IRQ mode");
                //}
#else
#error "Unknown driver mode!"
#endif
        }

        return err;
}

//==============================================================================
/**
 * @brief  Execute QSPI command.
 *
 * @param  hdl          device handle.
 * @param  command      command
 *
 * @return One of errno value.
 */
//==============================================================================
static int qspi_command(QSPI_block_t *hdl, const QSPI_command_t *command)
{
        if (not hdl->configured) {
                return EAGAIN;
        }

        if (QSPI.memory_mapped_mode) {
                dev_dbg(hdl, "command operation cancelled - memory mapped mode enabled");
                return EBUSY;
        }

        if (not is_command_config_valid(command)) {
                dev_dbg(hdl, "invalid command");
                return EINVAL;
        }

        int err = HAL_QSPI_SetFlashID(&QSPI.hqspi,
                (hdl->minor == 0) ? QSPI_FLASH_ID_1 : QSPI_FLASH_ID_2);
        if (err) {
                return err;
        }

        QSPI_CommandTypeDef hal_cmd;

        err = command_to_hal(command, &hal_cmd);
        if (err) {
                return EINVAL;
        }

        err = HAL_QSPI_Command(&QSPI.hqspi, &hal_cmd, __QSPI_CFG_TIMEOUT__);
        if (err) {
                return err;
        }

        if (command->data_size > 0) {
                if (command->direction == QSPI_DIRECTION__READ) {
#if (__QSPI_CFG_MODE__ == _QSPI_MODE_POLLING)
                        err = HAL_QSPI_Receive(&QSPI.hqspi, command->data,
                                __QSPI_CFG_TIMEOUT__);

#elif (__QSPI_CFG_MODE__ == _QSPI_MODE_IRQ)
                        err = HAL_QSPI_Receive_IT(&QSPI.hqspi, command->data);
                        if (not err) {
                                err = sys_semaphore_wait(QSPI.sem_xfer_complete,
                                        __QSPI_CFG_TIMEOUT__);
                                if (err) {
                                        HAL_QSPI_Abort(&QSPI.hqspi);
                                }
                        }

#elif (__QSPI_CFG_MODE__ == _QSPI_MODE_DMA)
                        //if (QSPI.hqspi.hdma) {
                        //        err = HAL_QSPI_Receive_DMA(&QSPI.hqspi, command->data);
                        //} else {
                                err = HAL_QSPI_Receive_IT(&QSPI.hqspi, command->data);
                        //}
                        if (not err) {
                                err = sys_semaphore_wait(QSPI.sem_xfer_complete,
                                        __QSPI_CFG_TIMEOUT__);
                                if (err) {
                                        HAL_QSPI_Abort(&QSPI.hqspi);
                                }
                        }
#else
#error "Unknown driver mode!"
#endif

                } else {
#if (__QSPI_CFG_MODE__ == _QSPI_MODE_POLLING)
                        err = HAL_QSPI_Transmit(&QSPI.hqspi, command->data,
                                __QSPI_CFG_TIMEOUT__);

#elif (__QSPI_CFG_MODE__ == _QSPI_MODE_IRQ)
                        err = HAL_QSPI_Transmit_IT(&QSPI.hqspi, command->data);
                        if (not err) {
                                err = sys_semaphore_wait(QSPI.sem_xfer_complete,
                                        __QSPI_CFG_TIMEOUT__);
                                if (err) {
                                        HAL_QSPI_Abort(&QSPI.hqspi);
                                }
                        }

#elif (__QSPI_CFG_MODE__ == _QSPI_MODE_DMA)
                        //if (QSPI.hqspi.hdma) {
                        //        err = HAL_QSPI_Transmit_DMA(&QSPI.hqspi, command->data);
                        //} else {
                                err = HAL_QSPI_Transmit_IT(&QSPI.hqspi, command->data);
                        //}
                        if (not err) {
                                err = sys_semaphore_wait(QSPI.sem_xfer_complete,
                                        __QSPI_CFG_TIMEOUT__);
                                if (err) {
                                        HAL_QSPI_Abort(&QSPI.hqspi);
                                }
                        }
#else
#error "Unknown driver mode!"
#endif
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Enable memory mapped mode.
 *
 * @param  hdl          device handle.
 * @param  command      command
 *
 * @return One of errno value.
 */
//==============================================================================
static int qspi_enter_memory_mapped_mode(QSPI_block_t *hdl, const QSPI_command_t *command)
{
        QSPI_CommandTypeDef hal_cmd;

        int err = command_to_hal(command, &hal_cmd);
        if (err) {
                return err;
        }

        QSPI_MemoryMappedTypeDef hal_mmm_cfg;
        hal_mmm_cfg.TimeOutActivation = __QSPI_CFG_MMM_TIMOUT_COUNTER__;
        hal_mmm_cfg.TimeOutPeriod     = __QSPI_CFG_MMM_TIMEOUT_CYCLES__;

        err = HAL_QSPI_MemoryMapped(&QSPI.hqspi, &hal_cmd, &hal_mmm_cfg);
        if (err) {
                dev_dbg(hdl, "unable to enter memory mapped mode");
        } else {
                QSPI.memory_mapped_mode = true;
                dev_dbg(hdl, "entered to memory mapped mode");
        }

        return err;
}

//==============================================================================
/**
 * @brief  Disable memory mapped mode.
 *
 * @param  hdl          device handle.
 *
 * @return One of errno value.
 */
//==============================================================================
static int qspi_exit_memory_mapped_mode(QSPI_block_t *hdl)
{
        int err = HAL_QSPI_Abort(&QSPI.hqspi);
        if (err) {
                dev_dbg(hdl, "unable to exit memory mapped mode");
        } else {
                QSPI.memory_mapped_mode = false;
                dev_dbg(hdl, "exit from memory mapped mode");
        }

        return err;
}

//==============================================================================
/**
 * @brief  Initialize peripheral.
 *
 * @param  hqspi        QSPI instance
 */
//==============================================================================
void HAL_QSPI_MspInit(QSPI_HandleTypeDef *hqspi)
{
        UNUSED_ARG1(hqspi);

        SET_BIT(RCC->AHB3ENR, RCC_AHB3ENR_QSPIEN);
        SET_BIT(RCC->AHB3RSTR, RCC_AHB3RSTR_QSPIRST);
        CLEAR_BIT(RCC->AHB3RSTR, RCC_AHB3RSTR_QSPIRST);

        NVIC_SetPriority(QUADSPI_IRQn, __QSPI_CFG_IRQ_PRIORITY__);
        NVIC_ClearPendingIRQ(QUADSPI_IRQn);
        NVIC_EnableIRQ(QUADSPI_IRQn);
}

//==============================================================================
/**
 * @brief  Deinitialize peripheral.
 *
 * @param  hqspi        QSPI instance
 */
//==============================================================================
void HAL_QSPI_MspDeInit(QSPI_HandleTypeDef *hqspi)
{
        UNUSED_ARG1(hqspi);

        NVIC_DisableIRQ(QUADSPI_IRQn);

        SET_BIT(RCC->AHB3RSTR, RCC_AHB3RSTR_QSPIRST);
        CLEAR_BIT(RCC->AHB3RSTR, RCC_AHB3RSTR_QSPIRST);
        CLEAR_BIT(RCC->AHB3ENR, RCC_AHB3ENR_QSPIEN);

        QSPI.hqspi.Instance = NULL;
}

//==============================================================================
/**
 * @brief  Rx complete callback (IRQ & DMA).
 *
 * @param  hqspi        QSPI instance
 */
//==============================================================================
void HAL_QSPI_RxCpltCallback(QSPI_HandleTypeDef *hqspi)
{
        UNUSED(hqspi);
        sys_semaphore_signal_from_ISR(QSPI.sem_xfer_complete, &QSPI.irq_yield);
}

//==============================================================================
/**
 * @brief  Tx complete callback (IRQ & DMA).
 *
 * @param  hqspi        QSPI instance
 */
//==============================================================================
void HAL_QSPI_TxCpltCallback(QSPI_HandleTypeDef *hqspi)
{
        UNUSED(hqspi);
        sys_semaphore_signal_from_ISR(QSPI.sem_xfer_complete, &QSPI.irq_yield);
}

//==============================================================================
/**
 * @brief  Tx complete callback (IRQ & DMA).
 *
 * @param  hqspi        QSPI instance
 */
//==============================================================================
void HAL_QSPI_StatusMatchCallback(QSPI_HandleTypeDef *hqspi)
{
        UNUSED(hqspi);
        sys_semaphore_signal_from_ISR(QSPI.sem_xfer_complete, &QSPI.irq_yield);
}

//==============================================================================
/**
 * @brief  QUADSPI interrupt.
 */
//==============================================================================
void QUADSPI_OCTOSPI1_IRQHandler(void)
{
        HAL_QSPI_IRQHandler(&QSPI.hqspi);

        sys_thread_yield_from_ISR(&QSPI.irq_yield);
}

/*==============================================================================
  End of file
==============================================================================*/
