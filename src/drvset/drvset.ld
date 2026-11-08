/*==============================================================================
@file    drvset.ld

@author  Daniel Zorychta

@brief   Linker script of drvset partition (drivers built separately from the
         kernel).

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

/*
 * The file is not a part of the kernel, it is used by the drvset build
 * (tools/drvset of the workspace). The build generates drvset_memory.ld
 * (search path of the linker) from the kernel configuration:
 *
 *   MEMORY
 *   {
 *      drvset_flash (rx)  : ORIGIN = __OS_DRVSET_FLASH_ADDRESS__, LENGTH = __OS_DRVSET_FLASH_SIZE__ KiB
 *      drvset_ram   (rwx) : ORIGIN = start of main RAM region of kernel, LENGTH = __OS_DRVSET_RAM_SIZE__ KiB
 *   }
 *   __ksymtab_vector = kernel vector table address + 4 * _KVECTOR_KSYMTAB;
 *
 * POSIX simulator: drvset_ram follows drvset_flash (mapped by the simulator),
 * __ksymtab_vector is the first word of RAM (8 bytes before drvset_ram).
 *
 * The kernel reserves drvset RAM at the start of its main RAM region
 * (.drvset_ram section) and checks all addresses of the header at start.
 */

/* output format is the default format of the toolchain (ARM or POSIX simulator) */

INCLUDE drvset_memory.ld

ENTRY(_drvset_header);

SECTIONS
{
   .text :
   {
      KEEP(*(.drvset_header));                  /* partition header, the first object */
      . = ALIGN(4);
      __drvset_irq_shared_start = .;            /* shared interrupt handlers (DRV_IRQ_SHARED()) */
      KEEP(*(.drvset_irq_shared));
      __drvset_irq_shared_end = .;
      . = ALIGN(4);
      *(.text .text.* .gnu.linkonce.t.*);
      . = ALIGN(4);
      *(.glue_7t .glue_7);
      . = ALIGN(4);
      *(.rodata .rodata.* .gnu.linkonce.r.*);
      . = ALIGN(4);
   } > drvset_flash AT > drvset_flash

   .ARM.extab :
   {
      *(.ARM.extab* .gnu.linkonce.armextab.*);
      . = ALIGN(4);
   } > drvset_flash AT > drvset_flash

   .ARM.exidx :
   {
      *(.ARM.exidx* .gnu.linkonce.armexidx.*);
      . = ALIGN(4);
   } > drvset_flash AT > drvset_flash

   /* C++ constructors are not supported (kernel does not call them) */
   .init_array :
   {
      *(.preinit_array .init_array .init_array.* .fini_array .fini_array.* .ctors .dtors);
   } > drvset_flash AT > drvset_flash

   .data :
   {
      . = ALIGN(8);
      __drvset_data_start = .;
      *(.data .data.* .gnu.linkonce.d.*);
      *(.got .got.plt);
      . = ALIGN(4);
      __drvset_data_end = .;
   } > drvset_ram AT > drvset_flash

   __drvset_data_load = LOADADDR(.data);

   .bss (NOLOAD) :
   {
      . = ALIGN(4);
      __drvset_bss_start = .;
      *(.bss .bss.* .gnu.linkonce.b.*);
      *(COMMON);
      . = ALIGN(4);
      __drvset_bss_end = .;
   } > drvset_ram

   __drvset_image_size = LOADADDR(.data) + SIZEOF(.data) - ORIGIN(drvset_flash);
   __drvset_irq_shared_count = (__drvset_irq_shared_end - __drvset_irq_shared_start) / 8;

   /DISCARD/ : { *(.note.GNU-stack) *(.drvset_ram) *(.eh_frame*) *(.note.gnu.property) *(.note.gnu.build-id) }
}

ASSERT(SIZEOF(.init_array) == 0, "drvset: C++ constructors and destructors are not supported");
ASSERT(_drvset_header == ORIGIN(drvset_flash), "drvset: partition header is not at the start of partition");

/*==============================================================================
  End of file
==============================================================================*/
