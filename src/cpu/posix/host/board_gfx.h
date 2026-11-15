/*==============================================================================
@file    board_gfx.h

@author  Daniel Zorychta

@brief   Graphics helpers of board models (POSIX simulator): memory buffers
         and canvases (framebuffer with tag buffer) drawn by models of display
         controllers, the canvas is copied to an LCD shown by the board panel.

@note    Copyright (C) 2026 Daniel Zorychta

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
 * NOTE: This file is used by the host part of the simulator only (board
 *       thread, Lua state of the board script).
 */

#ifndef _HOST_BOARD_GFX_H_
#define _HOST_BOARD_GFX_H_

/*==============================================================================
  Include files
==============================================================================*/
#include <stdint.h>

#include "lua/lua.h"

/*==============================================================================
  Exported functions
==============================================================================*/
/* push table of graphics functions (global "gfx" of board scripts) */
extern void board_gfx_open(lua_State *L);

/* implemented by board.c: write rows of canvas (0xRRGGBB pixels) to LCD object
 * at stack index idx (converted to LCD format, only changed rows), returns
 * number of written rows */
extern int board_lcd_write_canvas(lua_State *L, int idx, const uint32_t *px, int width, int height);

#endif /* _HOST_BOARD_GFX_H_ */

/*==============================================================================
  End of file
==============================================================================*/
