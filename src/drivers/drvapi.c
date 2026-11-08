/*==============================================================================
@file    drvapi.c

@author  Daniel Zorychta

@brief   Kernel API used only by drivers (drivers/driver.h).

@note    Copyright (C) 2026 Daniel Zorychta <daniel.zorychta@gmail.com>

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

/*==============================================================================
  Include files
==============================================================================*/
#include "kernel/sysfunc.h"
#include "drivers/driver.h"
#include "mm/mm.h"

/*==============================================================================
  Local macros
==============================================================================*/

/*==============================================================================
  Local object types
==============================================================================*/

/*==============================================================================
  Local function prototypes
==============================================================================*/

/*==============================================================================
  Local objects
==============================================================================*/

/*==============================================================================
  Exported objects
==============================================================================*/

/*==============================================================================
  External objects
==============================================================================*/

/*==============================================================================
  Function definitions
==============================================================================*/
//==============================================================================
/**
 * @brief  Function check if selected address is DMA capable.
 *
 * @param  ptr          address to examine
 *
 * @return If ptr is DMA capable then true is returned, otherwise false.
 *         NULL address is not DMA capable.
 */
//==============================================================================
bool drv_mem_is_dma_capable(const void *ptr)
{
        return _mm_is_dma_capable(ptr);
}

//==============================================================================
/**
 * @brief Function update all system clock after CPU frequency change.
 *
 * Function shall update all devices which base on main clock oscillator.
 * Function is called after clock/frequency change from clock management driver
 * (e.g. PLL).
 */
//==============================================================================
void drv_clock_changed(void)
{
        _cpuctl_update_system_clocks();
}

//==============================================================================
/**
 * @brief  Function register new memory region.
 *
 * @param  start        region start address
 * @param  size         region size
 * @param  flags        region features
 * @param  name         region name
 *
 * @return One of errno value.
 */
//==============================================================================
int drv_mem_register_region(void *start, size_t size, u32_t flags, const char *name)
{
        if (!name) {
                return EINVAL;
        }

        _mm_region_t *region = NULL;
        int err = _kzalloc(_MM_KRN, sizeof(*region) + strsize(name), 0, NULL, 0, 0, cast(void **, &region));
        if (!err) {
                char *region_name = cast(char *, region->extra);
                strcpy(region_name, name);

                err = _mm_register_region(region, start, size, flags, region_name);
                if (err) {
                        _kfree(_MM_KRN, cast(void **, &region));
                }
        }

        return err;
}

//==============================================================================
/**
 * @brief  Function checks if memory is cacheable.
 *
 * @param ptr       memory to examine
 *
 * @return If memory is cacheable then true is returned, otherwise false.
 */
//==============================================================================
bool drv_mem_is_cacheable(const void *ptr)
{
        return _mm_is_cacheable(ptr);
}

//==============================================================================
/**
 * @brief Function locks device for this process.
 *
 * @param dev_lock      pointer to device lock object
 *
 * @return One of errno value.
 */
//==============================================================================
int drv_lock(dev_lock_t *dev_lock)
{
        return _device_lock(dev_lock);
}

//==============================================================================
/**
 * @brief Function unlocks locked device.
 *
 * @param dev_lock      pointer to device lock object
 * @param force         true: force unlock
 *
 * @return One of errno value.
 */
//==============================================================================
int drv_unlock(dev_lock_t *dev_lock, bool force)
{
        return _device_unlock(dev_lock, force);
}

//==============================================================================
/**
 * @brief Function checks if current process has access to the device.
 *
 * @param dev_lock      pointer to device lock object
 *
 * @return One of errno value.
 */
//==============================================================================
int drv_check_access(dev_lock_t *dev_lock)
{
        return _device_get_access(dev_lock);
}

//==============================================================================
/**
 * @brief Function checks if device is locked.
 *
 * @param dev_lock      pointer to device lock object
 *
 * @return If device is locked then true is returned, otherwise false.
 */
//==============================================================================
bool drv_is_locked(dev_lock_t *dev_lock)
{
        return _device_is_locked(dev_lock);
}

//==============================================================================
/**
 * @brief  Function allocates memory for driver.
 *
 * @param module_name           module name (memory is accounted to the module)
 * @param size                  block size
 * @param align                 block alignment (0: default)
 * @param prefreg               preferred memory regions (NULL terminated, can be NULL)
 * @param required_features     required region features
 * @param required_mask         required features mask
 * @param zero                  clear allocated memory
 * @param mem                   allocated memory
 *
 * @return One of errno value.
 */
//==============================================================================
int drv_mem_alloc(const char *module_name, size_t size, size_t align, const char **prefreg, u32_t required_features,
        u32_t required_mask, bool zero, void **mem)
{
        int modid = _module_get_ID(module_name);

        if (zero) {
                return _kzalloc(_MM_MOD, size, align, prefreg, required_features, required_mask, mem, modid);
        } else {
                return _kmalloc(_MM_MOD, size, align, prefreg, required_features, required_mask, mem, modid);
        }
}

//==============================================================================
/**
 * @brief  Function frees memory of driver.
 *
 * @param module_name           module name
 * @param mem                   memory to free (pointer is cleared)
 *
 * @return One of errno value.
 */
//==============================================================================
int drv_mem_free(const char *module_name, void **mem)
{
        return _kfree(_MM_MOD, mem, _module_get_ID(module_name));
}

//==============================================================================
/**
 * @brief  Function returns instance of selected module.
 *
 * @param module_name           module name
 * @param major                 major number
 * @param minor                 minor number
 * @param mem                   driver instance memory
 *
 * @return One of errno value.
 */
//==============================================================================
int drv_module_get_instance(const char *module_name, u8_t major, u8_t minor, void **mem)
{
        return _module_get_instance(module_name, major, minor, mem);
}

/*==============================================================================
  End of file
==============================================================================*/
