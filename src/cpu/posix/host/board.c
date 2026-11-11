/*==============================================================================
@file    board.c

@author  Daniel Zorychta

@brief   Board model of the POSIX simulator. The board (hardware connected to
         the microcontroller) is described by a Lua script selected by the
         simulator option --board. The script is executed in a separate host
         thread that owns the Lua state; events of the system (e.g. GPIO
         output changes) are queued and handled in this thread. Bus
         transfers (I2C, SPI) are synchronous requests: the caller waits until
         the board thread handles the request.

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
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"

#include "board.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define GPIO_PORTS      26  /* PA..PZ */
#define GPIO_PINS       32
#define EVENT_QUEUE_LEN 1024
#define I2C_BUSES       16
#define SPI_BUSES       16
#define REQUEST_TIMEOUT 5000 /* ms */
#define PANEL_LINE_MAX  4096 /* max length of panel message */
#define PANEL_SEND_TMO  1000 /* ms */
#define LCD_REFRESH_MS  50   /* period of LCD refresh (changed lines are sent) */
#define LCD_MAX         8    /* max number of LCDs (LTDC driver and board models) */
#define LCD_ID_LEN      32   /* max length of LCD id (with terminating zero) */
#define LCD_SIZE_MAX    4096 /* max width and height of LCD of board model */
#define LCD_FORMATS     8
#define LCD_METATABLE   "board.lcd"
#define UART_PORTS      _HOST_BOARD_UART_PORTS
#define UART_FIFO_SIZE  4096 /* bytes waiting for reception (both directions) */
#define UART_TX_WINDOW  10.0 /* ms: transmitter accepts bytes sent in this time (driver polls UART) */
#define UART_OVERRUN_MS 200.0 /* ms: received byte not read by system in this time is lost */

/*==============================================================================
  Local object types
==============================================================================*/
typedef enum {
        EVENT__GPIO_OUTPUT,
        EVENT__PANEL_CONNECT,
        EVENT__PANEL_MESSAGE,
        EVENT__PANEL_DISCONNECT,
        EVENT__UART_CONFIG,
} event_type_t;

typedef struct {
        event_type_t type;
        int port;
        uint32_t state;
        uint32_t changed;
        char *text;             /* panel message (allocated) */
} event_t;

typedef enum {
        REQUEST__I2C_START,
        REQUEST__I2C_ADDRESS,
        REQUEST__I2C_WRITE,
        REQUEST__I2C_READ,
        REQUEST__I2C_STOP,
        REQUEST__SPI_TRANSFER,
} request_type_t;

/* synchronous request (caller waits for result) */
typedef struct {
        bool pending;           /* request waits for result */
        bool taken;             /* request is handled by board thread */
        uint32_t seq;           /* request number */
        request_type_t type;
        int bus;
        int address;
        bool read;
        const uint8_t *src;
        uint8_t *dst;
        size_t len;
        uint8_t fill;
        unsigned flags;
        int result;
        size_t done;
} request_t;

/* LCD shown by the panel: LTDC driver (slot 0, id "ltdc") or board model */
typedef struct {
        char id[LCD_ID_LEN];      /* empty: slot was never used */
        bool used;                /* slot is allocated */
        const uint8_t *fb;        /* framebuffer (NULL: LCD is not active) */
        uint8_t *own;             /* framebuffer of board model (allocated) */
        uint16_t width;
        uint16_t height;
        int format;
        uint8_t bg[3];
        unsigned version;         /* changed by attach, enable, release */

        /* used only by LCD thread */
        uint8_t *shadow;          /* copy of sent framebuffer */
        size_t shadow_size;
        char *enc;                /* base64 buffer */
        unsigned sent_version;
        bool sent_empty;
} lcd_t;

/* LCD object of board script (userdata) */
typedef struct {
        int slot;
} lcd_ref_t;

/* bytes with time of their reception */
typedef struct {
        uint8_t data[UART_FIFO_SIZE];
        double at[UART_FIFO_SIZE];
        size_t head;
        size_t tail;
} uart_fifo_t;

/* UART connected to board model */
typedef struct {
        bool allowed;             /* host allows connection of model */
        bool attached;            /* model is attached */
        bool timing;              /* transmission time of characters */
        uint32_t baud;
        int parity;               /* 0: none, 1: odd, 2: even */
        int stop_bits;
        double char_ms;           /* time of one character */
        double rx_line;           /* time when the last byte sent by model is received */
        double tx_line;           /* time when the last byte sent by system is transmitted */
        uart_fifo_t rx;           /* model -> system */
        uart_fifo_t tx;           /* system -> model */
        size_t overrun;           /* bytes not read by system (lost) */
        bool overrun_reported;    /* overrun reported (until system reads data) */
} uart_port_t;

/*==============================================================================
  Local function prototypes
==============================================================================*/
static void *board_thread(void *arg);

/*==============================================================================
  Local objects
==============================================================================*/
static struct {
        pthread_t thread;
        pthread_mutex_t mtx;
        pthread_cond_t cond;
        lua_State *L;
        const char *script;
        struct timespec t0;

        event_t queue[EVENT_QUEUE_LEN];
        size_t head;
        size_t tail;
        size_t lost;

        uint32_t out_state[GPIO_PORTS];   /* output state of the system */
        uint32_t drive_mask[GPIO_PORTS];  /* pins driven by board */
        uint32_t drive_level[GPIO_PORTS]; /* levels of driven pins */

        pthread_mutex_t req_mtx;          /* one request at a time */
        pthread_cond_t req_cond;          /* request finished */
        request_t req;

        int ref_next_timeout;
        int ref_run_timers;
        int ref_gpio_event;
        int ref_i2c_request;
        int ref_spi_request;
        int ref_run_exit;
        int ref_panel_event;
        int ref_uart_event;

        pthread_mutex_t uart_mtx;         /* UART ports */
        uart_port_t uart[UART_PORTS];
        bool uart_kick;                   /* board thread: new data for model */

        const char *panel_path;           /* panel socket path (NULL: no panel) */
        pthread_t panel_thread;
        int panel_listen;                 /* listening socket */
        int panel_client;                 /* connected panel (-1: none) */
        unsigned panel_conn;              /* number of panel connections */
        pthread_mutex_t panel_send_mtx;   /* messages are not mixed */
        bool panel_started;

        struct {
                pthread_mutex_t mtx;
                pthread_t thread;
                lcd_t slot[LCD_MAX];      /* slot 0: LTDC driver */
        } lcd;

        bool init_done;
        int init_result;
        bool running;
        bool stop;
        bool exit_done;
} board = {
        .mtx = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .req_mtx = PTHREAD_MUTEX_INITIALIZER,
        .req_cond = PTHREAD_COND_INITIALIZER,
        .panel_listen = -1,
        .panel_client = -1,
        .panel_send_mtx = PTHREAD_MUTEX_INITIALIZER,
        .uart_mtx = PTHREAD_MUTEX_INITIALIZER,
        .lcd = {.mtx = PTHREAD_MUTEX_INITIALIZER, .slot = {[0] = {.id = "ltdc", .used = true}}},
};

/* pixel formats (the same order as in the LTDC driver) */
static const char *const LCD_FORMAT[LCD_FORMATS + 1] = {"argb8888", "rgb888", "rgb565", "argb1555",
                                                        "argb4444", "l8",     "al44",   "al88", NULL};
static const uint8_t LCD_PIXEL_SIZE[LCD_FORMATS] = {4, 3, 2, 2, 2, 1, 1, 2};

/*
 * Board API implemented in Lua. The chunk gets the table of primitives
 * implemented in C and returns functions called by the board thread.
 */
static const char PRELUDE[] =
        "local B = ...\n"
        "\n"
        "-- log -------------------------------------------------------------------\n"
        "local function tostr(...)\n"
        "    local t = {}\n"
        "    for i = 1, select('#', ...) do t[i] = tostring((select(i, ...))) end\n"
        "    return table.concat(t, '\\t')\n"
        "end\n"
        "print = function(...) B.log(tostr(...)) end\n"
        "\n"
        "-- call user function, error is printed (other handlers are called)\n"
        "local function call(fn, ...)\n"
        "    local ok, err = xpcall(fn, debug.traceback, ...)\n"
        "    if not ok then print(err) end\n"
        "end\n"
        "\n"
        "-- host ------------------------------------------------------------------\n"
        "host = {}\n"
        "host.time_ms = B.time_ms\n"
        "host.log = print\n"
        "host.script = B.SCRIPT\n"
        "host.dir = string.match(B.SCRIPT, '^(.*)/[^/]*$') or '.'\n"
        "\n"
        "local timers = {}\n"
        "\n"
        "-- call fn after ms (periodic: every ms), returns timer object with stop()\n"
        "function host.timer(ms, fn, periodic)\n"
        "    assert(type(ms) == 'number' and ms >= 0, 'host.timer: invalid time')\n"
        "    assert(type(fn) == 'function', 'host.timer: function expected')\n"
        "    local t = {due = B.time_ms() + ms, period = periodic and math.max(ms, 1) or nil,\n"
        "               fn = fn, active = true}\n"
        "    function t:stop() self.active = false end\n"
        "    timers[#timers + 1] = t\n"
        "    return t\n"
        "end\n"
        "\n"
        "local exit_handlers = {}\n"
        "\n"
        "-- call fn() at simulator exit or restart (e.g. save state of model)\n"
        "function host.on_exit(fn)\n"
        "    assert(type(fn) == 'function', 'host.on_exit: function expected')\n"
        "    table.insert(exit_handlers, fn)\n"
        "end\n"
        "\n"
        "local function run_exit()\n"
        "    for _, fn in ipairs(exit_handlers) do call(fn) end\n"
        "end\n"
        "\n"
        "local function next_timeout()\n"
        "    local now, tmo = B.time_ms(), nil\n"
        "    for _, t in ipairs(timers) do\n"
        "        if t.active then\n"
        "            local d = math.max(t.due - now, 0)\n"
        "            if not tmo or d < tmo then tmo = d end\n"
        "        end\n"
        "    end\n"
        "    return tmo or -1\n"
        "end\n"
        "\n"
        "local function run_timers()\n"
        "    local now = B.time_ms()\n"
        "    for _, t in ipairs({table.unpack(timers)}) do\n"
        "        if t.active and t.due <= now then\n"
        "            if t.period then\n"
        "                t.due = t.due + t.period\n"
        "                if t.due <= now then t.due = now + t.period end\n"
        "            else\n"
        "                t.active = false\n"
        "            end\n"
        "            call(t.fn, t)\n"
        "        end\n"
        "    end\n"
        "    for i = #timers, 1, -1 do\n"
        "        if not timers[i].active then table.remove(timers, i) end\n"
        "    end\n"
        "end\n"
        "\n"
        "-- gpio ------------------------------------------------------------------\n"
        "gpio = {}\n"
        "\n"
        "local handlers = {}\n"
        "\n"
        "-- pin name in STM32 style: PA0 (port A, pin 0) .. PZ31\n"
        "local function pin(name)\n"
        "    local p, n = string.match(tostring(name), '^P(%u)(%d+)$')\n"
        "    n = tonumber(n)\n"
        "    if not p or n > 31 then error('invalid pin name: ' .. tostring(name), 3) end\n"
        "    return p:byte() - 65, n\n"
        "end\n"
        "\n"
        "local function level(v)\n"
        "    if type(v) == 'number' then return v ~= 0 end\n"
        "    return v and true or false\n"
        "end\n"
        "\n"
        "-- observers of pin level seen by the system (panel)\n"
        "local watchers = {}\n"
        "\n"
        "local function watch_pin(p, n, fn)\n"
        "    local key = p * 32 + n\n"
        "    watchers[key] = watchers[key] or {}\n"
        "    table.insert(watchers[key], fn)\n"
        "end\n"
        "\n"
        "local function notify(p, n)\n"
        "    local list = watchers[p * 32 + n]\n"
        "    if list then\n"
        "        local v = B.level(p, n)\n"
        "        for _, fn in ipairs(list) do call(fn, v) end\n"
        "    end\n"
        "end\n"
        "\n"
        "-- drive input pin of the system (true/1: high, false/0: low)\n"
        "function gpio.set(name, v) local p, n = pin(name); B.drive(p, n, level(v)); notify(p, n) end\n"
        "\n"
        "-- stop driving pin (system sees its own output state)\n"
        "function gpio.release(name) local p, n = pin(name); B.drive(p, n, nil); notify(p, n) end\n"
        "\n"
        "-- pin level seen by the system\n"
        "function gpio.get(name) local p, n = pin(name); return B.level(p, n) end\n"
        "\n"
        "-- call fn(level, name) when system changes output state of pin\n"
        "function gpio.on_change(name, fn)\n"
        "    assert(type(fn) == 'function', 'gpio.on_change: function expected')\n"
        "    local p, n = pin(name)\n"
        "    local key = p * 32 + n\n"
        "    handlers[key] = handlers[key] or {}\n"
        "    table.insert(handlers[key], {fn = fn, name = name})\n"
        "end\n"
        "\n"
        "-- connect system output to system input (wire), optionally inverted\n"
        "function gpio.connect(out_name, in_name, invert)\n"
        "    local function update(v)\n"
        "        if invert then v = not v end\n"
        "        gpio.set(in_name, v)\n"
        "    end\n"
        "    gpio.on_change(out_name, update)\n"
        "    update(gpio.get(out_name))\n"
        "end\n"
        "\n"
        "local function gpio_event(port, state, changed)\n"
        "    for n = 0, 31 do\n"
        "        if (changed >> n) & 1 == 1 then\n"
        "            local list = handlers[port * 32 + n]\n"
        "            if list then\n"
        "                local v = (state >> n) & 1 == 1\n"
        "                for _, h in ipairs(list) do call(h.fn, v, h.name) end\n"
        "            end\n"
        "            notify(port, n)\n"
        "        end\n"
        "    end\n"
        "end\n"
        "\n"
        "-- i2c -------------------------------------------------------------------\n"
        "i2c = {}\n"
        "\n"
        "local buses = {}\n"
        "\n"
        "-- peripheral name as in driver_init(): '<driver>:<major>', e.g. 'I2C:0'\n"
        "-- ('UART:1' is /dev/ttyS1), returns major of the driver\n"
        "local function major_of(driver, name, count, level)\n"
        "    local n = tonumber(string.match(tostring(name), '^' .. driver .. ':(%d+)$'))\n"
        "    if not n or n >= count then\n"
        "        error(string.format(\"invalid %s name: %s (expected '%s:<major>', major 0..%d)\",\n"
        "                            driver, tostring(name), driver, count - 1), level + 1)\n"
        "    end\n"
        "    return n\n"
        "end\n"
        "\n"
        "local function bus_index(name)\n"
        "    local n = major_of('I2C', name, B.I2C_BUSES, 3)   -- no tail call (error level)\n"
        "    return n\n"
        "end\n"
        "\n"
        "local function get_bus(idx)\n"
        "    local b = buses[idx]\n"
        "    if not b then\n"
        "        b = {devices = {}, active = {}}\n"
        "        buses[idx] = b\n"
        "    end\n"
        "    return b\n"
        "end\n"
        "\n"
        "local function addr_key(address, ten_bit, fn)\n"
        "    local max = ten_bit and 0x3FF or 0x7F\n"
        "    if math.type(address) ~= 'integer' or address < 0 or address > max then\n"
        "        error(fn .. ': invalid address: ' .. tostring(address), 3)\n"
        "    end\n"
        "    return ten_bit and (address | B.I2C_ADDR_10BIT) or address\n"
        "end\n"
        "\n"
        "-- attach device model to bus (7-bit address, 10-bit if ten_bit is true)\n"
        "function i2c.attach(bus, address, dev, ten_bit)\n"
        "    local b = get_bus(bus_index(bus))\n"
        "    local key = addr_key(address, ten_bit, 'i2c.attach')\n"
        "    if type(dev) ~= 'table' then error('i2c.attach: device table expected', 2) end\n"
        "    if b.devices[key] then\n"
        "        error(string.format('i2c.attach: address 0x%02X is used on %s', address, bus), 2)\n"
        "    end\n"
        "    b.devices[key] = dev\n"
        "    return dev\n"
        "end\n"
        "\n"
        "-- detach device from bus\n"
        "function i2c.detach(bus, address, ten_bit)\n"
        "    local b = get_bus(bus_index(bus))\n"
        "    b.devices[addr_key(address, ten_bit, 'i2c.detach')] = nil\n"
        "end\n"
        "\n"
        "-- device attached to bus address\n"
        "function i2c.device(bus, address, ten_bit)\n"
        "    local b = get_bus(bus_index(bus))\n"
        "    return b.devices[addr_key(address, ten_bit, 'i2c.device')]\n"
        "end\n"
        "\n"
        "-- call device method (error is printed and false is returned)\n"
        "local function dcall(dev, method, ...)\n"
        "    local fn = dev[method]\n"
        "    if fn == nil then return true, nil end\n"
        "    local ok, r = xpcall(fn, debug.traceback, dev, ...)\n"
        "    if not ok then print(r) end\n"
        "    return ok, r\n"
        "end\n"
        "\n"
        "local ACK, NACK = 0, 1\n"
        "\n"
        "local function i2c_request(op, idx, a, b)\n"
        "    local bus = get_bus(idx)\n"
        "\n"
        "    if op == 0 then -- START or repeated START\n"
        "        bus.sel = nil\n"
        "        return ACK\n"
        "\n"
        "    elseif op == 1 then -- address: a = address key, b = read\n"
        "        bus.sel = nil\n"
        "        local dev = bus.devices[a]\n"
        "        if not dev then return NACK end\n"
        "        local ok, r = dcall(dev, 'select', b)\n"
        "        if not ok or r == false then return NACK end\n"
        "        bus.sel, bus.read = dev, b\n"
        "        local found = false\n"
        "        for _, d in ipairs(bus.active) do if d == dev then found = true end end\n"
        "        if not found then table.insert(bus.active, dev) end\n"
        "        return ACK\n"
        "\n"
        "    elseif op == 2 then -- write: a = data, returns status, number of acknowledged bytes\n"
        "        local dev = bus.sel\n"
        "        if not dev or bus.read then return NACK, 0 end\n"
        "        for i = 1, #a do\n"
        "            local ok, r = dcall(dev, 'write', a:byte(i))\n"
        "            if not ok or r == false then return NACK, i - 1 end\n"
        "        end\n"
        "        return ACK, #a\n"
        "\n"
        "    elseif op == 3 then -- read: a = count, returns status, data\n"
        "        local dev = bus.sel\n"
        "        if not dev or not bus.read then return NACK, '' end\n"
        "        local t = {}\n"
        "        for i = 1, a do\n"
        "            local ok, r = dcall(dev, 'read', i == a)\n"
        "            r = ok and math.tointeger(r) or 0xFF\n"
        "            t[i] = string.char(r & 0xFF)\n"
        "        end\n"
        "        return ACK, table.concat(t)\n"
        "\n"
        "    elseif op == 4 then -- STOP\n"
        "        bus.sel = nil\n"
        "        local list = bus.active\n"
        "        bus.active = {}\n"
        "        for _, dev in ipairs(list) do dcall(dev, 'stop') end\n"
        "        return ACK\n"
        "    end\n"
        "end\n"
        "\n"
        "-- spi -------------------------------------------------------------------\n"
        "spi = {}\n"
        "\n"
        "local spi_buses = {}\n"
        "\n"
        "local function spi_bus_index(name)\n"
        "    local n = major_of('SPI', name, B.SPI_BUSES, 3)   -- no tail call (error level)\n"
        "    return n\n"
        "end\n"
        "\n"
        "-- bit reverse table (LSB first transfers)\n"
        "local REV = {}\n"
        "for v = 0, 255 do\n"
        "    local r = 0\n"
        "    for b = 0, 7 do if (v >> b) & 1 == 1 then r = r | (1 << (7 - b)) end end\n"
        "    REV[v] = r\n"
        "end\n"
        "\n"
        "-- attach device model to bus, device is selected by CS pin (nil: always\n"
        "-- selected), CS is active low (opts.cs_active_high: active high)\n"
        "function spi.attach(bus, cs, dev, opts)\n"
        "    local idx = spi_bus_index(bus)\n"
        "    if type(dev) ~= 'table' then error('spi.attach: device table expected', 2) end\n"
        "    if cs ~= nil then pin(cs) end\n"
        "    spi_buses[idx] = spi_buses[idx] or {}\n"
        "    local e = {dev = dev, cs = cs, active = (opts and opts.cs_active_high) and true or false}\n"
        "    if cs then\n"
        "        e.selected = gpio.get(cs) == e.active\n"
        "        gpio.on_change(cs, function(level)\n"
        "            local sel = level == e.active\n"
        "            if sel ~= e.selected then\n"
        "                e.selected = sel\n"
        "                dcall(dev, sel and 'select' or 'deselect')\n"
        "            end\n"
        "        end)\n"
        "    else\n"
        "        e.selected = true\n"
        "    end\n"
        "    table.insert(spi_buses[idx], e)\n"
        "    return dev\n"
        "end\n"
        "\n"
        "-- exchange data with selected devices (one protected call for transfer)\n"
        "local function spi_exchange(sel, data)\n"
        "    local out, char = {}, string.char\n"
        "    for i = 1, #data do\n"
        "        local tx, r = data:byte(i), 0xFF\n"
        "        for j = 1, #sel do\n"
        "            local e = sel[j]\n"
        "            local fn = e.dev.transfer\n"
        "            local v = fn and math.tointeger(fn(e.dev, e.rev and REV[tx] or tx)) or 0xFF\n"
        "            v = v & 0xFF\n"
        "            r = r & (e.rev and REV[v] or v)\n"
        "        end\n"
        "        out[i] = char(r)\n"
        "    end\n"
        "    return table.concat(out)\n"
        "end\n"
        "\n"
        "-- full duplex transfer: data sent by system, returns data received by system\n"
        "local function spi_request(idx, data, flags)\n"
        "    local mode, lsb = flags & 3, (flags & B.SPI_LSB_FIRST) ~= 0\n"
        "    local sel = {}\n"
        "    for _, e in ipairs(spi_buses[idx] or {}) do\n"
        "        if e.selected then\n"
        "            local m = e.dev.mode\n"
        "            if m ~= nil and m ~= mode and not (type(m) == 'table' and m[mode]) then\n"
        "                if e.warned ~= mode then\n"
        "                    e.warned = mode\n"
        "                    print(string.format('SPI:%d: device does not support mode %d: no response',\n"
        "                                        idx, mode))\n"
        "                end\n"
        "            else\n"
        "                e.rev = (e.dev.lsb_first and true or false) ~= lsb\n"
        "                sel[#sel + 1] = e\n"
        "            end\n"
        "        end\n"
        "    end\n"
        "    if #sel == 0 then return string.rep('\\xFF', #data) end\n"
        "    local ok, res = xpcall(spi_exchange, debug.traceback, sel, data)\n"
        "    if not ok then\n"
        "        print(res)\n"
        "        return string.rep('\\xFF', #data)\n"
        "    end\n"
        "    return res\n"
        "end\n"
        "\n"
        "-- uart ------------------------------------------------------------------\n"
        "uart = {}\n"
        "\n"
        "local uart_devs = {}\n"
        "local uart_full = {}\n"
        "local PARITY = {[0] = 'none', 'odd', 'even'}\n"
        "\n"
        "-- 'UART:<major>': /dev/ttyS<major>, simulator option --uart <major>\n"
        "local function uart_index(name)\n"
        "    local n = major_of('UART', name, B.UART_PORTS, 3)   -- no tail call (error level)\n"
        "    return n\n"
        "end\n"
        "\n"
        "-- attach device model to UART: dev:receive(data) gets data sent by the system,\n"
        "-- dev:configure(cfg) the configuration set by the system; opts.timing = false:\n"
        "-- data is transferred immediately (default: transmission time of characters)\n"
        "function uart.attach(name, dev, opts)\n"
        "    local idx = uart_index(name)\n"
        "    if type(dev) ~= 'table' then error('uart.attach: device table expected', 2) end\n"
        "    local ok, err = B.uart_attach(idx, not (opts and opts.timing == false))\n"
        "    if not ok then error('uart.attach: ' .. tostring(name) .. ': ' .. err, 2) end\n"
        "    uart_devs[idx] = dev\n"
        "    return dev\n"
        "end\n"
        "\n"
        "-- UART can be attached (not connected by simulator option --uart, not used)\n"
        "function uart.available(name)\n"
        "    return B.uart_available(uart_index(name))\n"
        "end\n"
        "\n"
        "-- send data to the system, returns number of queued bytes\n"
        "function uart.send(name, data)\n"
        "    local idx = uart_index(name)\n"
        "    data = tostring(data)\n"
        "    local n = B.uart_send(idx, data)\n"
        "    if n < #data and not uart_full[idx] then\n"
        "        print(string.format('%s: model sends data faster than baud rate, buffer full, '\n"
        "                            .. 'data is lost', name))\n"
        "    end\n"
        "    if n < #data then uart_full[idx] = true elseif B.uart_pending(idx) == 0 then uart_full[idx] = false end\n"
        "    return n\n"
        "end\n"
        "\n"
        "-- configuration set by the system: {baud = , parity = 'none'|'odd'|'even', stop_bits = }\n"
        "function uart.config(name)\n"
        "    local baud, parity, stop = B.uart_config(uart_index(name))\n"
        "    return {baud = baud, parity = PARITY[parity] or 'none', stop_bits = stop}\n"
        "end\n"
        "\n"
        "-- number of bytes sent by model and not yet received by the system\n"
        "function uart.pending(name)\n"
        "    return B.uart_pending(uart_index(name))\n"
        "end\n"
        "\n"
        "-- UART event: 0 data sent by the system, 1 configuration changed\n"
        "local function uart_event(kind, idx, data)\n"
        "    local dev = uart_devs[idx]\n"
        "    if not dev then return end\n"
        "    if kind == 0 then\n"
        "        dcall(dev, 'receive', data)\n"
        "    else\n"
        "        dcall(dev, 'configure', uart.config('UART:' .. idx))\n"
        "    end\n"
        "end\n"
        "\n"
        "-- panel -----------------------------------------------------------------\n"
        "panel = {}\n"
        "\n"
        "local signals, order = {}, {}     -- id -> {id, kind, ui, value}, definition order\n"
        "local controls = {}               -- id -> list of fn(value, id)\n"
        "local dirty, dirty_list = {}, {}\n"
        "local page_name = string.match(B.SCRIPT, '([^/]+)%.lua$') or 'default'\n"
        "local connected = false\n"
        "local flush_timer = nil\n"
        "local FLUSH_MS = 20\n"
        "\n"
        "local ARRAY = {}\n"
        "\n"
        "local ESC = {['\"'] = '\\\\\"', ['\\\\'] = '\\\\\\\\', ['\\n'] = '\\\\n', ['\\r'] = '\\\\r', ['\\t'] = '\\\\t'}\n"
        "\n"
        "local function json(v)\n"
        "    local t = type(v)\n"
        "    if t == 'string' then\n"
        "        return '\"' .. (v:gsub('[%c\"\\\\]', function(c)\n"
        "            return ESC[c] or string.format('\\\\u%04x', c:byte())\n"
        "        end)) .. '\"'\n"
        "    elseif t == 'number' then\n"
        "        if v ~= v or v == math.huge or v == -math.huge then return 'null' end\n"
        "        if math.type(v) == 'integer' then return tostring(v) end\n"
        "        return string.format('%.14g', v)\n"
        "    elseif t == 'boolean' then\n"
        "        return tostring(v)\n"
        "    elseif t == 'table' then\n"
        "        local out = {}\n"
        "        if getmetatable(v) == ARRAY or #v > 0 then\n"
        "            for i = 1, #v do out[i] = json(v[i]) end\n"
        "            return '[' .. table.concat(out, ',') .. ']'\n"
        "        end\n"
        "        local keys = {}\n"
        "        for k in pairs(v) do keys[#keys + 1] = tostring(k) end\n"
        "        table.sort(keys)\n"
        "        for i, k in ipairs(keys) do out[i] = json(k) .. ':' .. json(v[k]) end\n"
        "        return '{' .. table.concat(out, ',') .. '}'\n"
        "    end\n"
        "    return 'null'\n"
        "end\n"
        "\n"
        "local function send(obj)\n"
        "    if connected and not B.panel_send(json(obj)) then\n"
        "        connected = false\n"
        "    end\n"
        "end\n"
        "\n"
        "local function send_init()\n"
        "    local list, state = setmetatable({}, ARRAY), {}\n"
        "    for _, id in ipairs(order) do\n"
        "        local e = signals[id]\n"
        "        list[#list + 1] = {id = id, kind = e.kind, ui = e.ui}\n"
        "        if e.value ~= nil then state[id] = e.value end\n"
        "    end\n"
        "    dirty, dirty_list = {}, {}\n"
        "    send({t = 'init', page = page_name, board = B.SCRIPT, signals = list, state = state})\n"
        "end\n"
        "\n"
        "local function flush()\n"
        "    flush_timer = nil\n"
        "    local state = {}\n"
        "    for _, id in ipairs(dirty_list) do state[id] = signals[id].value end\n"
        "    dirty, dirty_list = {}, {}\n"
        "    if next(state) ~= nil then send({t = 'state', state = state}) end\n"
        "end\n"
        "\n"
        "local function publish(id, v)\n"
        "    local e = signals[id]\n"
        "    if e.value == v and type(v) ~= 'table' then return end\n"
        "    e.value = v\n"
        "    if not connected then return end\n"
        "    if not dirty[id] then\n"
        "        dirty[id] = true\n"
        "        dirty_list[#dirty_list + 1] = id\n"
        "    end\n"
        "    if not flush_timer then flush_timer = host.timer(FLUSH_MS, flush) end\n"
        "end\n"
        "\n"
        "local function define(id, kind, opts, value)\n"
        "    if type(id) ~= 'string' or not string.match(id, '^[%w_%.%-]+$') then\n"
        "        error('panel: invalid signal id: ' .. tostring(id), 3)\n"
        "    end\n"
        "    if signals[id] then error('panel: signal ' .. id .. ' is already defined', 3) end\n"
        "    local ui = {}\n"
        "    for k, v in pairs(opts or {}) do\n"
        "        local t = type(v)\n"
        "        if type(k) == 'string' and (t == 'string' or t == 'number' or t == 'boolean') then ui[k] = v end\n"
        "    end\n"
        "    signals[id] = {id = id, kind = kind, ui = ui, value = value}\n"
        "    order[#order + 1] = id\n"
        "    if connected then send_init() end\n"
        "end\n"
        "\n"
        "local function add_control(id, fn)\n"
        "    controls[id] = controls[id] or {}\n"
        "    table.insert(controls[id], fn)\n"
        "end\n"
        "\n"
        "-- select page of the panel (directory sim-env/panel/<name>, default: board script name)\n"
        "function panel.page(name)\n"
        "    page_name = tostring(name)\n"
        "    if connected then send_init() end\n"
        "end\n"
        "\n"
        "-- show pin level seen by the system (e.g. LED), opts.active_low: inverted\n"
        "function panel.output(id, name, opts)\n"
        "    local p, n = pin(name)\n"
        "    local inv = (opts and opts.active_low) and true or false\n"
        "    define(id, 'output', opts, B.level(p, n) ~= inv)\n"
        "    watch_pin(p, n, function(v) publish(id, v ~= inv) end)\n"
        "end\n"
        "\n"
        "-- panel control drives input pin of the system (button, switch),\n"
        "-- opts.active_low: inverted, opts.value: initial state (default false)\n"
        "function panel.input(id, name, opts)\n"
        "    pin(name)\n"
        "    local inv = (opts and opts.active_low) and true or false\n"
        "    local init = (opts and opts.value) and true or false\n"
        "    define(id, 'input', opts, init)\n"
        "    gpio.set(name, init ~= inv)\n"
        "    add_control(id, function(v)\n"
        "        v = level(v)\n"
        "        gpio.set(name, v ~= inv)\n"
        "        publish(id, v)\n"
        "    end)\n"
        "end\n"
        "\n"
        "-- value shown by the panel: initial value or function polled every\n"
        "-- opts.period ms (default 500), changed by panel.set()\n"
        "function panel.value(id, v, opts)\n"
        "    if type(v) == 'function' then\n"
        "        local fn = v\n"
        "        define(id, 'value', opts, fn())\n"
        "        host.timer((opts and opts.period) or 500, function() publish(id, fn()) end, true)\n"
        "    else\n"
        "        define(id, 'value', opts, v)\n"
        "    end\n"
        "end\n"
        "\n"
        "-- panel control (slider, field, button): fn(value, id) is called when the\n"
        "-- value is changed in the panel, opts.value: initial value\n"
        "function panel.control(id, fn, opts)\n"
        "    assert(type(fn) == 'function', 'panel.control: function expected')\n"
        "    define(id, 'control', opts, opts and opts.value)\n"
        "    add_control(id, function(v, i)\n"
        "        publish(id, v)\n"
        "        fn(v, i)\n"
        "    end)\n"
        "end\n"
        "\n"
        "-- set value of signal (sent to the panel)\n"
        "function panel.set(id, v)\n"
        "    if not signals[id] then error('panel.set: unknown signal: ' .. tostring(id), 2) end\n"
        "    publish(id, v)\n"
        "end\n"
        "\n"
        "-- current value of signal\n"
        "function panel.get(id)\n"
        "    local e = signals[id]\n"
        "    return e and e.value\n"
        "end\n"
        "\n"
        "-- panel is connected\n"
        "function panel.connected()\n"
        "    return connected\n"
        "end\n"
        "\n"
        "local touch_handlers = {}\n"
        "\n"
        "-- call fn(x, y, pressed, lcd_id) when LCD of the panel is touched (pressed\n"
        "-- false: released), lcd_id: only touch of this LCD (default: all LCDs)\n"
        "function panel.on_touch(fn, lcd_id)\n"
        "    assert(type(fn) == 'function', 'panel.on_touch: function expected')\n"
        "    table.insert(touch_handlers, {fn = fn, id = lcd_id})\n"
        "end\n"
        "\n"
        "-- LCD of board model (e.g. model of LCD controller) shown by the panel\n"
        "-- (<canvas data-lcd=\"<id>\">), id \"ltdc\" is the LCD of the LTDC driver,\n"
        "-- opts.format: pixel format (default rgb565), opts.bg: {r, g, b} background\n"
        "-- color (blended with alpha of pixels), opts.enabled: false - not shown\n"
        "local lcds = {}\n"
        "local LCD = B.LCD_METHODS\n"
        "\n"
        "function LCD:on_touch(fn) panel.on_touch(fn, self.id) end\n"
        "\n"
        "function panel.lcd(id, width, height, opts)\n"
        "    opts = opts or {}\n"
        "    if type(id) ~= 'string' or not string.match(id, '^[%w_%.%-]+$') then\n"
        "        error('panel.lcd: invalid LCD id: ' .. tostring(id), 2)\n"
        "    end\n"
        "    local bg = opts.bg or {}\n"
        "    local ok, d = pcall(B.lcd_new, id, width, height, opts.format or 'rgb565', bg[1], bg[2], bg[3])\n"
        "    if not ok then error(d, 2) end\n"
        "    if opts.enabled == false then d:enable(false) end\n"
        "    lcds[id] = d      -- LCD exists until the board is stopped\n"
        "    return d\n"
        "end\n"
        "\n"
        "-- panel event: 0 connected, 1 message, 2 disconnected\n"
        "local function panel_event(kind, text)\n"
        "    if kind == 0 then\n"
        "        connected = true\n"
        "        send_init()\n"
        "    elseif kind == 2 then\n"
        "        connected = false\n"
        "    elseif kind == 1 and string.sub(text, 1, 6) == 'touch ' then\n"
        "        -- touch <x> <y> <pressed: 0/1> <LCD id>\n"
        "        local x, y, p, id = string.match(text, '^touch (%d+) (%d+) ([01]) ([%w_%.%-]+)$')\n"
        "        if not x then\n"
        "            print('panel: invalid message: ' .. text)\n"
        "        else\n"
        "            for _, h in ipairs(touch_handlers) do\n"
        "                if h.id == nil or h.id == id then call(h.fn, tonumber(x), tonumber(y), p == '1', id) end\n"
        "            end\n"
        "        end\n"
        "    elseif kind == 1 then\n"
        "        -- set <id> <type><value>, type: b (0/1), n (number), s (string)\n"
        "        local id, t, v = string.match(text, '^set ([%w_%.%-]+) (%a)(.*)$')\n"
        "        if t == 'b' then v = (v == '1')\n"
        "        elseif t == 'n' then v = tonumber(v)\n"
        "        elseif t ~= 's' then v = nil end\n"
        "        if not id or v == nil then\n"
        "            print('panel: invalid message: ' .. text)\n"
        "        elseif not controls[id] then\n"
        "            print('panel: signal is not a control: ' .. id)\n"
        "        else\n"
        "            for _, fn in ipairs(controls[id]) do call(fn, v, id) end\n"
        "        end\n"
        "    end\n"
        "end\n"
        "\n"
        "return next_timeout, run_timers, gpio_event, i2c_request, run_exit, spi_request, panel_event, uart_event\n";

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Print board message (simulator terminal can be in raw mode).
 *
 * @param  msg          message
 */
//==============================================================================
static void board_log(const char *msg)
{
        const char *p = msg;
        fputs("[board] ", stderr);

        while (*p) {
                const char *nl = strchr(p, '\n');
                size_t len = nl ? (size_t)(nl - p) : strlen(p);
                fwrite(p, 1, len, stderr);
                if (!nl) {
                        break;
                }
                fputs("\r\n[board] ", stderr);
                p = nl + 1;
        }

        fputs("\r\n", stderr);
        fflush(stderr);
}

//==============================================================================
/**
 * @brief  Time from board start in milliseconds.
 */
//==============================================================================
static double time_ms(void)
{
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return (double)(t.tv_sec - board.t0.tv_sec) * 1000.0 + (double)(t.tv_nsec - board.t0.tv_nsec) / 1e6;
}

//==============================================================================
/**
 * @brief  Lua: B.time_ms()
 */
//==============================================================================
static int l_time_ms(lua_State *L)
{
        lua_pushnumber(L, time_ms());
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: B.log(str)
 */
//==============================================================================
static int l_log(lua_State *L)
{
        board_log(luaL_checkstring(L, 1));
        return 0;
}

//==============================================================================
/**
 * @brief  Lua: B.drive(port, pin, level|nil)
 */
//==============================================================================
static int l_drive(lua_State *L)
{
        int port = (int)luaL_checkinteger(L, 1);
        int pin = (int)luaL_checkinteger(L, 2);
        luaL_argcheck(L, (port >= 0) && (port < GPIO_PORTS), 1, "invalid port");
        luaL_argcheck(L, (pin >= 0) && (pin < GPIO_PINS), 2, "invalid pin");

        uint32_t mask = 1UL << pin;

        pthread_mutex_lock(&board.mtx);
        if (lua_isnil(L, 3)) {
                board.drive_mask[port] &= ~mask;
        } else {
                board.drive_mask[port] |= mask;
                if (lua_toboolean(L, 3)) {
                        board.drive_level[port] |= mask;
                } else {
                        board.drive_level[port] &= ~mask;
                }
        }
        pthread_mutex_unlock(&board.mtx);

        return 0;
}

//==============================================================================
/**
 * @brief  Lua: B.level(port, pin) -> level seen by the system
 */
//==============================================================================
static int l_level(lua_State *L)
{
        int port = (int)luaL_checkinteger(L, 1);
        int pin = (int)luaL_checkinteger(L, 2);
        luaL_argcheck(L, (port >= 0) && (port < GPIO_PORTS), 1, "invalid port");
        luaL_argcheck(L, (pin >= 0) && (pin < GPIO_PINS), 2, "invalid pin");

        uint32_t mask = 1UL << pin;

        pthread_mutex_lock(&board.mtx);
        uint32_t state = (board.out_state[port] & ~board.drive_mask[port])
                       | (board.drive_level[port] & board.drive_mask[port]);
        pthread_mutex_unlock(&board.mtx);

        lua_pushboolean(L, (state & mask) != 0);
        return 1;
}

//==============================================================================
/**
 * @brief  Send message (line) to the connected panel. Message consists of
 *         parts, new line is added. The connection is closed on error (e.g. the
 *         panel does not read data).
 *
 * @param  part         message parts
 * @param  size         sizes of parts
 * @param  count        number of parts
 *
 * @return true on success.
 */
//==============================================================================
static bool panel_write(const char *const part[], const size_t size[], size_t count)
{
        pthread_mutex_lock(&board.panel_send_mtx);

        pthread_mutex_lock(&board.mtx);
        int fd = board.panel_client;
        pthread_mutex_unlock(&board.mtx);

        bool ok = (fd >= 0);

        for (size_t i = 0; ok && (i <= count); i++) {
                const char *p = (i < count) ? part[i] : "\n";
                size_t n = (i < count) ? size[i] : 1;

                while (n > 0) {
                        ssize_t r = send(fd, p, n, MSG_NOSIGNAL);
                        if (r > 0) {
                                p += r;
                                n -= (size_t)r;
                        } else if ((r < 0) && (errno == EINTR)) {
                                continue;
                        } else {
                                ok = false;
                                break;
                        }
                }
        }

        if (!ok && (fd >= 0)) {
                /* reader thread closes the connection */
                shutdown(fd, SHUT_RDWR);
        }

        pthread_mutex_unlock(&board.panel_send_mtx);

        return ok;
}

//==============================================================================
/**
 * @brief  Lua: B.panel_send(line) -> true on success. Line (JSON) is sent to the
 *         connected panel.
 */
//==============================================================================
static int l_panel_send(lua_State *L)
{
        size_t len;
        const char *msg = luaL_checklstring(L, 1, &len);

        lua_pushboolean(L, panel_write(&msg, &len, 1));
        return 1;
}

//==============================================================================
/**
 * @brief  LCD slot of LCD object (argument of Lua function).
 *
 * @param  L            Lua state
 * @param  size         size of framebuffer in bytes (can be NULL)
 *
 * @return LCD slot.
 */
//==============================================================================
static lcd_t *check_lcd(lua_State *L, size_t *size)
{
        lcd_ref_t *ref = luaL_checkudata(L, 1, LCD_METATABLE);
        luaL_argcheck(L, ref->slot > 0, 1, "LCD is released");

        lcd_t *lcd = &board.lcd.slot[ref->slot];

        if (size) {
                *size = (size_t)lcd->width * lcd->height * LCD_PIXEL_SIZE[lcd->format];
        }

        return lcd;
}

//==============================================================================
/**
 * @brief  Write pixel value (little endian, as in the framebuffer of LTDC).
 *
 * @param  dst          destination
 * @param  value        pixel value
 * @param  size         pixel size in bytes
 */
//==============================================================================
static void lcd_put(uint8_t *dst, uint32_t value, int size)
{
        for (int i = 0; i < size; i++) {
                dst[i] = (uint8_t)(value >> (8 * i));
        }
}

//==============================================================================
/**
 * @brief  Lua: B.lcd_new(id, width, height, format, r, g, b) -> LCD object
 *         (framebuffer of board model shown by the panel)
 */
//==============================================================================
static int l_lcd_new(lua_State *L)
{
        const char *id = luaL_checkstring(L, 1);
        lua_Integer width = luaL_checkinteger(L, 2);
        lua_Integer height = luaL_checkinteger(L, 3);
        int format = luaL_checkoption(L, 4, "rgb565", LCD_FORMAT);
        uint8_t bg[3];

        for (int i = 0; i < 3; i++) {
                bg[i] = (uint8_t)luaL_optinteger(L, 5 + i, 0);
        }

        luaL_argcheck(L, (strlen(id) > 0) && (strlen(id) < LCD_ID_LEN), 1, "invalid LCD id");
        luaL_argcheck(L, (width > 0) && (width <= LCD_SIZE_MAX), 2, "invalid width");
        luaL_argcheck(L, (height > 0) && (height <= LCD_SIZE_MAX), 3, "invalid height");

        size_t size = (size_t)width * (size_t)height * LCD_PIXEL_SIZE[format];
        uint8_t *fb = calloc(1, size);
        if (!fb) {
                return luaL_error(L, "panel.lcd: out of memory");
        }

        lcd_ref_t *ref = lua_newuserdatauv(L, sizeof(lcd_ref_t), 0);
        ref->slot = 0;
        luaL_setmetatable(L, LCD_METATABLE);

        const char *err = NULL;

        pthread_mutex_lock(&board.lcd.mtx);

        for (int i = 0; i < LCD_MAX; i++) {
                if (board.lcd.slot[i].used && (strcmp(board.lcd.slot[i].id, id) == 0)) {
                        err = "panel.lcd: LCD %s is already defined";
                        break;
                }
        }

        for (int i = 1; !err && (i < LCD_MAX); i++) {
                lcd_t *lcd = &board.lcd.slot[i];
                if (!lcd->used) {
                        strcpy(lcd->id, id);
                        lcd->used = true;
                        lcd->own = fb;
                        lcd->fb = fb;
                        lcd->width = (uint16_t)width;
                        lcd->height = (uint16_t)height;
                        lcd->format = format;
                        memcpy(lcd->bg, bg, sizeof(bg));
                        lcd->version++;
                        ref->slot = i;
                        break;
                }
        }

        if (!err && (ref->slot == 0)) {
                err = "panel.lcd: too many LCDs (%s)";
        }

        pthread_mutex_unlock(&board.lcd.mtx);

        if (err) {
                free(fb);
                return luaL_error(L, err, id);
        }

        return 1;
}

//==============================================================================
/**
 * @brief  Lua: lcd:write(offset, data) -> number of written bytes
 *         (raw data of framebuffer, offset in bytes)
 */
//==============================================================================
static int l_lcd_write(lua_State *L)
{
        size_t size, len;
        lcd_t *lcd = check_lcd(L, &size);
        lua_Integer offset = luaL_checkinteger(L, 2);
        const char *data = luaL_checklstring(L, 3, &len);

        size_t n = 0;
        if ((offset >= 0) && ((size_t)offset < size)) {
                n = (len < size - (size_t)offset) ? len : size - (size_t)offset;
                pthread_mutex_lock(&board.lcd.mtx);
                memcpy(lcd->own + offset, data, n);
                pthread_mutex_unlock(&board.lcd.mtx);
        }

        lua_pushinteger(L, (lua_Integer)n);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: lcd:read(offset, len) -> data (raw data of framebuffer)
 */
//==============================================================================
static int l_lcd_read(lua_State *L)
{
        size_t size;
        lcd_t *lcd = check_lcd(L, &size);
        lua_Integer offset = luaL_checkinteger(L, 2);
        lua_Integer len = luaL_checkinteger(L, 3);

        if ((offset < 0) || ((size_t)offset >= size) || (len <= 0)) {
                lua_pushliteral(L, "");
        } else {
                size_t n = ((size_t)len < size - (size_t)offset) ? (size_t)len : size - (size_t)offset;
                lua_pushlstring(L, (const char *)lcd->own + offset, n); /* written only by this thread */
        }

        return 1;
}

//==============================================================================
/**
 * @brief  Lua: lcd:pixel(x, y [, value]) -> value (pixel value in LCD format,
 *         pixels out of LCD are not written, nil is returned)
 */
//==============================================================================
static int l_lcd_pixel(lua_State *L)
{
        lcd_t *lcd = check_lcd(L, NULL);
        lua_Integer x = luaL_checkinteger(L, 2);
        lua_Integer y = luaL_checkinteger(L, 3);

        if ((x < 0) || (y < 0) || (x >= lcd->width) || (y >= lcd->height)) {
                lua_pushnil(L);
                return 1;
        }

        int ps = LCD_PIXEL_SIZE[lcd->format];
        uint8_t *p = lcd->own + ((size_t)y * lcd->width + (size_t)x) * ps;

        if (!lua_isnoneornil(L, 4)) {
                uint32_t v = (uint32_t)luaL_checkinteger(L, 4);
                pthread_mutex_lock(&board.lcd.mtx);
                lcd_put(p, v, ps);
                pthread_mutex_unlock(&board.lcd.mtx);
        }

        uint32_t v = 0;
        for (int i = 0; i < ps; i++) {
                v |= (uint32_t)p[i] << (8 * i);
        }

        lua_pushinteger(L, v);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: lcd:fill(value [, x, y, w, h]) (rectangle is clipped)
 */
//==============================================================================
static int l_lcd_fill(lua_State *L)
{
        lcd_t *lcd = check_lcd(L, NULL);
        uint32_t v = (uint32_t)luaL_checkinteger(L, 2);
        lua_Integer x = luaL_optinteger(L, 3, 0);
        lua_Integer y = luaL_optinteger(L, 4, 0);
        lua_Integer w = luaL_optinteger(L, 5, lcd->width);
        lua_Integer h = luaL_optinteger(L, 6, lcd->height);

        if (x < 0) {
                w += x;
                x = 0;
        }

        if (y < 0) {
                h += y;
                y = 0;
        }

        if (w > lcd->width - x) {
                w = lcd->width - x;
        }

        if (h > lcd->height - y) {
                h = lcd->height - y;
        }

        if ((w > 0) && (h > 0)) {
                int ps = LCD_PIXEL_SIZE[lcd->format];
                pthread_mutex_lock(&board.lcd.mtx);
                for (lua_Integer j = y; j < y + h; j++) {
                        uint8_t *p = lcd->own + ((size_t)j * lcd->width + (size_t)x) * ps;
                        for (lua_Integer i = 0; i < w; i++, p += ps) {
                                lcd_put(p, v, ps);
                        }
                }
                pthread_mutex_unlock(&board.lcd.mtx);
        }

        return 0;
}

//==============================================================================
/**
 * @brief  Lua: lcd:color(r, g, b [, a]) -> pixel value in LCD format
 *         (components 0..255, l8/al44/al88: luminance)
 */
//==============================================================================
static int l_lcd_color(lua_State *L)
{
        lcd_t *lcd = check_lcd(L, NULL);
        uint32_t c[4];

        for (int i = 0; i < 4; i++) {
                c[i] = (uint32_t)luaL_optinteger(L, 2 + i, 255) & 0xFF;
        }

        uint32_t r = c[0], g = c[1], b = c[2], a = c[3];
        uint32_t l = (r * 77 + g * 150 + b * 29) >> 8;
        uint32_t v = 0;

        switch (lcd->format) {
        case 0: v = (a << 24) | (r << 16) | (g << 8) | b; break;                                 /* argb8888 */
        case 1: v = (r << 16) | (g << 8) | b; break;                                             /* rgb888 */
        case 2: v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); break;                        /* rgb565 */
        case 3: v = ((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); break;     /* argb1555 */
        case 4: v = ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4); break;      /* argb4444 */
        case 5: v = l; break;                                                                    /* l8 */
        case 6: v = ((a >> 4) << 4) | (l >> 4); break;                                           /* al44 */
        case 7: v = (a << 8) | l; break;                                                         /* al88 */
        }

        lua_pushinteger(L, v);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: lcd:enable(on) (disabled LCD is not shown by the panel)
 */
//==============================================================================
static int l_lcd_enable(lua_State *L)
{
        lcd_t *lcd = check_lcd(L, NULL);
        const uint8_t *fb = lua_toboolean(L, 2) ? lcd->own : NULL;

        pthread_mutex_lock(&board.lcd.mtx);
        if (lcd->fb != fb) {
                lcd->fb = fb;
                lcd->version++;
        }
        pthread_mutex_unlock(&board.lcd.mtx);

        return 0;
}

//==============================================================================
/**
 * @brief  Lua: lcd fields: id, width, height, format, pixel_size, enabled;
 *         methods from the method table.
 */
//==============================================================================
static int l_lcd_index(lua_State *L)
{
        lcd_t *lcd = check_lcd(L, NULL);
        const char *key = luaL_checkstring(L, 2);

        if (strcmp(key, "id") == 0) {
                lua_pushstring(L, lcd->id);
        } else if (strcmp(key, "width") == 0) {
                lua_pushinteger(L, lcd->width);
        } else if (strcmp(key, "height") == 0) {
                lua_pushinteger(L, lcd->height);
        } else if (strcmp(key, "format") == 0) {
                lua_pushstring(L, LCD_FORMAT[lcd->format]);
        } else if (strcmp(key, "pixel_size") == 0) {
                lua_pushinteger(L, LCD_PIXEL_SIZE[lcd->format]);
        } else if (strcmp(key, "enabled") == 0) {
                lua_pushboolean(L, lcd->fb != NULL);
        } else {
                lua_getfield(L, lua_upvalueindex(1), key);
        }

        return 1;
}

//==============================================================================
/**
 * @brief  Lua: LCD object is collected: LCD is removed from the panel.
 */
//==============================================================================
static int l_lcd_gc(lua_State *L)
{
        lcd_ref_t *ref = luaL_checkudata(L, 1, LCD_METATABLE);

        if (ref->slot > 0) {
                pthread_mutex_lock(&board.lcd.mtx);
                lcd_t *lcd = &board.lcd.slot[ref->slot];
                free(lcd->own);
                lcd->own = NULL;
                lcd->fb = NULL;
                lcd->used = false;
                lcd->version++;
                pthread_mutex_unlock(&board.lcd.mtx);
                ref->slot = 0;
        }

        return 0;
}

//==============================================================================
/**
 * @brief  Create metatable of LCD objects, the method table (can be extended
 *         by board API in Lua) is left on the stack.
 */
//==============================================================================
static void lcd_metatable(lua_State *L)
{
        static const luaL_Reg METHODS[] = {
                {"write", l_lcd_write},
                {"read", l_lcd_read},
                {"pixel", l_lcd_pixel},
                {"fill", l_lcd_fill},
                {"color", l_lcd_color},
                {"enable", l_lcd_enable},
                {NULL, NULL},
        };

        luaL_newlib(L, METHODS);                /* methods */
        luaL_newmetatable(L, LCD_METATABLE);    /* methods, mt */
        lua_pushvalue(L, -2);
        lua_pushcclosure(L, l_lcd_index, 1);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, l_lcd_gc);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);                          /* methods */
}

//==============================================================================
/**
 * @brief  Message handler: adds traceback to error message.
 */
//==============================================================================
static int traceback(lua_State *L)
{
        const char *msg = lua_tostring(L, 1);
        luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
        return 1;
}

//==============================================================================
/**
 * @brief  Call function from registry with arguments on stack (protected).
 *
 * @param  ref          function reference
 * @param  nargs        number of arguments (on stack)
 * @param  nres         number of results
 *
 * @return On success true is returned (results on stack).
 */
//==============================================================================
static bool call_ref(int ref, int nargs, int nres)
{
        lua_State *L = board.L;

        lua_pushcfunction(L, traceback);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_rotate(L, -(nargs + 2), 2); /* msgh, func, args... */

        int base = lua_gettop(L) - nargs - 1;
        if (lua_pcall(L, nargs, nres, base) != LUA_OK) {
                board_log(lua_tostring(L, -1));
                lua_settop(L, base - 1);
                return false;
        }

        lua_remove(L, base);
        return true;
}

//==============================================================================
/**
 * @brief  Number of bytes in FIFO.
 */
//==============================================================================
static size_t fifo_count(const uart_fifo_t *f)
{
        return (f->head + UART_FIFO_SIZE - f->tail) % UART_FIFO_SIZE;
}

//==============================================================================
/**
 * @brief  Put byte to FIFO.
 *
 * @return true if byte was queued (false: FIFO is full).
 */
//==============================================================================
static bool fifo_put(uart_fifo_t *f, uint8_t byte, double at)
{
        size_t next = (f->head + 1) % UART_FIFO_SIZE;
        if (next == f->tail) {
                return false;
        }
        f->data[f->head] = byte;
        f->at[f->head] = at;
        f->head = next;
        return true;
}

//==============================================================================
/**
 * @brief  Get bytes received until selected time.
 *
 * @return Number of bytes.
 */
//==============================================================================
static size_t fifo_get(uart_fifo_t *f, uint8_t *dst, size_t len, double now)
{
        size_t n = 0;
        while ((n < len) && (f->tail != f->head) && (f->at[f->tail] <= now)) {
                dst[n++] = f->data[f->tail];
                f->tail = (f->tail + 1) % UART_FIFO_SIZE;
        }
        return n;
}

//==============================================================================
/**
 * @brief  Receiver overrun: drop bytes received by system that are not read in
 *         UART_OVERRUN_MS (system does not read UART). Called with locked UART
 *         mutex.
 */
//==============================================================================
static void uart_overrun(uart_port_t *u, double now)
{
        uart_fifo_t *f = &u->rx;
        while ((f->tail != f->head) && (f->at[f->tail] < now - UART_OVERRUN_MS)) {
                f->tail = (f->tail + 1) % UART_FIFO_SIZE;
                u->overrun++;
        }
}

//==============================================================================
/**
 * @brief  Set UART configuration (called with locked UART mutex).
 */
//==============================================================================
static void uart_set_config(uart_port_t *u, uint32_t baud, int parity, int stop_bits)
{
        u->baud = baud;
        u->parity = parity;
        u->stop_bits = stop_bits;
        /* start bit, 8 data bits, parity, stop bits */
        u->char_ms = (double)(1 + 8 + (parity ? 1 : 0) + stop_bits) * 1000.0 / (double)baud;
}

//==============================================================================
/**
 * @brief  Get UART of Lua argument (index).
 */
//==============================================================================
static int check_uart(lua_State *L, int arg)
{
        int port = (int)luaL_checkinteger(L, arg);
        luaL_argcheck(L, (port >= 0) && (port < UART_PORTS), arg, "invalid UART");
        return port;
}

//==============================================================================
/**
 * @brief  Lua: B.uart_attach(port, timing) -> true or nil, message
 */
//==============================================================================
static int l_uart_attach(lua_State *L)
{
        int port = check_uart(L, 1);
        bool timing = lua_toboolean(L, 2);
        const char *err = NULL;

        pthread_mutex_lock(&board.uart_mtx);
        uart_port_t *u = &board.uart[port];
        if (!u->allowed) {
                err = "UART is connected to the terminal, PTY or serial port (simulator option --uart)";
        } else if (u->attached) {
                err = "model is already attached";
        } else {
                u->attached = true;
                u->timing = timing;
        }
        pthread_mutex_unlock(&board.uart_mtx);

        if (err) {
                luaL_pushfail(L);
                lua_pushstring(L, err);
                return 2;
        }

        lua_pushboolean(L, true);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: B.uart_available(port) -> boolean
 */
//==============================================================================
static int l_uart_available(lua_State *L)
{
        int port = check_uart(L, 1);

        pthread_mutex_lock(&board.uart_mtx);
        bool available = board.uart[port].allowed && !board.uart[port].attached;
        pthread_mutex_unlock(&board.uart_mtx);

        lua_pushboolean(L, available);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: B.uart_send(port, data) -> number of queued bytes
 */
//==============================================================================
static int l_uart_send(lua_State *L)
{
        int port = check_uart(L, 1);
        size_t len;
        const uint8_t *data = (const uint8_t *)luaL_checklstring(L, 2, &len);
        size_t n = 0;

        pthread_mutex_lock(&board.uart_mtx);
        uart_port_t *u = &board.uart[port];
        if (u->attached) {
                double now = time_ms();
                uart_overrun(u, now);
                if (u->rx_line < now) {
                        u->rx_line = now;
                }
                for (; n < len; n++) {
                        double at = u->timing ? (u->rx_line + u->char_ms) : now;
                        if (!fifo_put(&u->rx, data[n], at)) {
                                break;
                        }
                        if (u->timing) {
                                u->rx_line = at;
                        }
                }
        }
        pthread_mutex_unlock(&board.uart_mtx);

        lua_pushinteger(L, (lua_Integer)n);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: B.uart_config(port) -> baud, parity, stop_bits
 */
//==============================================================================
static int l_uart_config(lua_State *L)
{
        int port = check_uart(L, 1);

        pthread_mutex_lock(&board.uart_mtx);
        uint32_t baud = board.uart[port].baud;
        int parity = board.uart[port].parity;
        int stop_bits = board.uart[port].stop_bits;
        pthread_mutex_unlock(&board.uart_mtx);

        lua_pushinteger(L, baud);
        lua_pushinteger(L, parity);
        lua_pushinteger(L, stop_bits);
        return 3;
}

//==============================================================================
/**
 * @brief  Lua: B.uart_pending(port) -> bytes not received by system yet
 */
//==============================================================================
static int l_uart_pending(lua_State *L)
{
        int port = check_uart(L, 1);

        pthread_mutex_lock(&board.uart_mtx);
        uart_overrun(&board.uart[port], time_ms());
        size_t n = fifo_count(&board.uart[port].rx);
        pthread_mutex_unlock(&board.uart_mtx);

        lua_pushinteger(L, (lua_Integer)n);
        return 1;
}

//==============================================================================
/**
 * @brief  Time to the next byte transmitted by system (board thread).
 *
 * @return Time in milliseconds or negative value if there is no data.
 */
//==============================================================================
static double uart_next_timeout(void)
{
        double tmo = -1;
        double now = time_ms();

        pthread_mutex_lock(&board.uart_mtx);
        for (int i = 0; i < UART_PORTS; i++) {
                uart_fifo_t *f = &board.uart[i].tx;
                if (f->tail != f->head) {
                        double d = f->at[f->tail] - now;
                        d = (d < 0) ? 0 : d;
                        if ((tmo < 0) || (d < tmo)) {
                                tmo = d;
                        }
                }
        }
        pthread_mutex_unlock(&board.uart_mtx);

        return tmo;
}

//==============================================================================
/**
 * @brief  Deliver bytes transmitted by system to models (board thread).
 */
//==============================================================================
static void uart_deliver(void)
{
        static uint8_t buf[UART_FIFO_SIZE];

        for (int i = 0; i < UART_PORTS; i++) {
                pthread_mutex_lock(&board.uart_mtx);
                uart_port_t *u = &board.uart[i];
                double now = time_ms();
                size_t n = fifo_get(&u->tx, buf, sizeof(buf), now);
                uart_overrun(u, now);
                size_t lost = 0;
                if ((u->overrun > 0) && !u->overrun_reported) {
                        lost = u->overrun;
                        u->overrun_reported = true;
                }
                pthread_mutex_unlock(&board.uart_mtx);

                if (lost) {
                        char msg[128];
                        snprintf(msg, sizeof(msg),
                                "UART:%d: system does not read received data, bytes are lost "
                                "(receiver overrun)",
                                i);
                        board_log(msg);
                }

                if (n > 0) {
                        lua_pushinteger(board.L, 0);
                        lua_pushinteger(board.L, i);
                        lua_pushlstring(board.L, (const char *)buf, n);
                        call_ref(board.ref_uart_event, 3, 0);
                }
        }
}

//==============================================================================
/**
 * @brief  Get time to the next timer.
 *
 * @return Time in milliseconds or negative value if there is no timer.
 */
//==============================================================================
static double next_timeout(void)
{
        double tmo = -1;

        if (call_ref(board.ref_next_timeout, 0, 1)) {
                tmo = lua_tonumber(board.L, -1);
                lua_pop(board.L, 1);
        }

        return tmo;
}

//==============================================================================
/**
 * @brief  Handle SPI transfer request (board thread). Called with locked
 *         mutex, the mutex is unlocked during Lua call.
 *
 * @param  req          request
 */
//==============================================================================
static void handle_spi_request(const request_t *req)
{
        lua_State *L = board.L;

        /* data are copied, caller can give up waiting (timeout) */
        uint8_t *data = malloc(req->len);
        if (data) {
                if (req->src) {
                        memcpy(data, req->src, req->len);
                } else {
                        memset(data, req->fill, req->len);
                }
        }

        pthread_mutex_unlock(&board.mtx);

        int result = -1;
        const char *rdata = NULL;
        size_t rlen = 0;
        int top = lua_gettop(L);

        if (data) {
                lua_pushinteger(L, req->bus);
                lua_pushlstring(L, (const char *)data, req->len);
                lua_pushinteger(L, req->flags);

                if (call_ref(board.ref_spi_request, 3, 1)) {
                        rdata = lua_tolstring(L, -1, &rlen);
                        result = (rdata && (rlen == req->len)) ? 0 : -1;
                }

                free(data);
        }

        pthread_mutex_lock(&board.mtx);

        if (board.req.pending && (board.req.seq == req->seq)) {
                if (req->dst) {
                        if (result == 0) {
                                memcpy(req->dst, rdata, req->len);
                        } else {
                                memset(req->dst, 0xFF, req->len);
                        }
                }

                board.req.result = result;
                board.req.pending = false;
                pthread_cond_broadcast(&board.req_cond);
        }

        lua_settop(L, top);
}

//==============================================================================
/**
 * @brief  Handle pending request (board thread). Called with locked mutex,
 *         the mutex is unlocked during Lua call.
 */
//==============================================================================
static void handle_request(void)
{
        lua_State *L = board.L;
        request_t req = board.req;
        board.req.taken = true;

        if (req.type == REQUEST__SPI_TRANSFER) {
                handle_spi_request(&req);
                return;
        }

        /* data are copied, caller can give up waiting (timeout) */
        uint8_t *data = NULL;
        if (req.type == REQUEST__I2C_WRITE) {
                data = malloc(req.len ? req.len : 1);
                if (data) {
                        memcpy(data, req.src, req.len);
                }
        }

        pthread_mutex_unlock(&board.mtx);

        int result = _HOST_BOARD_I2C_ERROR;
        size_t done = 0;
        const char *rdata = NULL;
        size_t rlen = 0;
        int top = lua_gettop(L);

        lua_pushinteger(L, req.type - REQUEST__I2C_START);
        lua_pushinteger(L, req.bus);

        int nargs = 2;
        bool ok = true;

        switch (req.type) {
        case REQUEST__I2C_ADDRESS:
                lua_pushinteger(L, req.address);
                lua_pushboolean(L, req.read);
                nargs += 2;
                break;
        case REQUEST__I2C_WRITE:
                if (data) {
                        lua_pushlstring(L, (const char *)data, req.len);
                        nargs += 1;
                } else {
                        ok = false;
                }
                break;
        case REQUEST__I2C_READ:
                lua_pushinteger(L, (lua_Integer)req.len);
                nargs += 1;
                break;
        default:
                break;
        }

        if (ok && call_ref(board.ref_i2c_request, nargs, 2)) {
                result = lua_tointeger(L, -2) == 0 ? _HOST_BOARD_I2C_ACK : _HOST_BOARD_I2C_NACK;

                if (req.type == REQUEST__I2C_WRITE) {
                        done = (size_t)lua_tointeger(L, -1);
                } else if (req.type == REQUEST__I2C_READ) {
                        rdata = lua_tolstring(L, -1, &rlen);
                }
        }

        free(data);

        pthread_mutex_lock(&board.mtx);

        if (board.req.pending && (board.req.seq == req.seq)) {
                if (req.type == REQUEST__I2C_READ) {
                        size_t n = rdata ? (rlen < req.len ? rlen : req.len) : 0;
                        if (n) {
                                memcpy(req.dst, rdata, n);
                        }
                        memset(req.dst + n, 0xFF, req.len - n);
                        if ((result == _HOST_BOARD_I2C_ACK) && (n < req.len)) {
                                result = _HOST_BOARD_I2C_ERROR;
                        }
                }

                board.req.result = result;
                board.req.done = done;
                board.req.pending = false;
                pthread_cond_broadcast(&board.req_cond);
        }

        lua_settop(L, top);
}

//==============================================================================
/**
 * @brief  Load board script and API (board thread).
 *
 * @return On success true is returned.
 */
//==============================================================================
static bool board_load(void)
{
        lua_State *L = luaL_newstate();
        if (!L) {
                board_log("unable to create Lua state");
                return false;
        }

        board.L = L;
        luaL_openlibs(L);

        /* modules are searched in the script directory and in its parent (sim-env) */
        char *path = strdup(board.script);
        char *dir = path ? dirname(path) : ".";
        lua_getglobal(L, "package");
        lua_pushfstring(L, "%s/?.lua;%s/?/init.lua;%s/../?.lua;%s/../?/init.lua", dir, dir, dir, dir);
        lua_setfield(L, -2, "path");
        lua_pop(L, 1);
        free(path);

        /* primitives */
        static const luaL_Reg PRIMITIVES[] = {
                {"time_ms", l_time_ms},
                {"log", l_log},
                {"drive", l_drive},
                {"level", l_level},
                {"panel_send", l_panel_send},
                {"lcd_new", l_lcd_new},
                {"uart_attach", l_uart_attach},
                {"uart_available", l_uart_available},
                {"uart_send", l_uart_send},
                {"uart_config", l_uart_config},
                {"uart_pending", l_uart_pending},
                {NULL, NULL},
        };

        lua_pushcfunction(L, traceback);
        if (luaL_loadbuffer(L, PRELUDE, sizeof(PRELUDE) - 1, "=board-api") != LUA_OK) {
                board_log(lua_tostring(L, -1));
                return false;
        }

        luaL_newlib(L, PRIMITIVES);
        lua_pushinteger(L, I2C_BUSES);
        lua_setfield(L, -2, "I2C_BUSES");
        lua_pushinteger(L, SPI_BUSES);
        lua_setfield(L, -2, "SPI_BUSES");
        lua_pushinteger(L, UART_PORTS);
        lua_setfield(L, -2, "UART_PORTS");
        lua_pushinteger(L, _HOST_BOARD_SPI_LSB_FIRST);
        lua_setfield(L, -2, "SPI_LSB_FIRST");
        lua_pushinteger(L, _HOST_BOARD_I2C_ADDR_10BIT);
        lua_setfield(L, -2, "I2C_ADDR_10BIT");
        lua_pushstring(L, board.script);
        lua_setfield(L, -2, "SCRIPT");
        lcd_metatable(L);
        lua_setfield(L, -2, "LCD_METHODS");

        if (lua_pcall(L, 1, 8, 1) != LUA_OK) {
                board_log(lua_tostring(L, -1));
                return false;
        }

        board.ref_uart_event = luaL_ref(L, LUA_REGISTRYINDEX);
        board.ref_panel_event = luaL_ref(L, LUA_REGISTRYINDEX);
        board.ref_spi_request = luaL_ref(L, LUA_REGISTRYINDEX);
        board.ref_run_exit = luaL_ref(L, LUA_REGISTRYINDEX);
        board.ref_i2c_request = luaL_ref(L, LUA_REGISTRYINDEX);
        board.ref_gpio_event = luaL_ref(L, LUA_REGISTRYINDEX);
        board.ref_run_timers = luaL_ref(L, LUA_REGISTRYINDEX);
        board.ref_next_timeout = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_settop(L, 0);

        /* board description */
        lua_pushcfunction(L, traceback);
        if (luaL_loadfile(L, board.script) != LUA_OK) {
                board_log(lua_tostring(L, -1));
                return false;
        }

        if (lua_pcall(L, 0, 0, 1) != LUA_OK) {
                board_log(lua_tostring(L, -1));
                return false;
        }

        lua_settop(L, 0);
        return true;
}

//==============================================================================
/**
 * @brief  Board thread: owns Lua state, handles events and timers.
 */
//==============================================================================
static void *board_thread(void *arg)
{
        (void)arg;

        bool ok = board_load();

        pthread_mutex_lock(&board.mtx);
        board.init_result = ok ? 0 : -1;
        board.init_done = true;
        board.running = ok;
        pthread_cond_broadcast(&board.cond);
        pthread_mutex_unlock(&board.mtx);

        if (!ok) {
                return NULL;
        }

        static event_t events[EVENT_QUEUE_LEN];

        for (;;) {
                double tmo = next_timeout();
                double uart_tmo = uart_next_timeout();
                if ((uart_tmo >= 0) && ((tmo < 0) || (uart_tmo < tmo))) {
                        tmo = uart_tmo;
                }

                pthread_mutex_lock(&board.mtx);

                bool request = board.req.pending && !board.req.taken;

                if (!board.stop && (board.head == board.tail) && !request && !board.uart_kick) {
                        if (tmo < 0) {
                                pthread_cond_wait(&board.cond, &board.mtx);
                        } else if (tmo > 0) {
                                struct timespec t;
                                clock_gettime(CLOCK_REALTIME, &t);
                                long long ns = (long long)t.tv_nsec + (long long)(tmo * 1e6);
                                t.tv_sec += (time_t)(ns / 1000000000LL);
                                t.tv_nsec = (long)(ns % 1000000000LL);
                                pthread_cond_timedwait(&board.cond, &board.mtx, &t);
                        }
                }

                size_t n = 0;
                while ((board.head != board.tail) && (n < EVENT_QUEUE_LEN)) {
                        events[n++] = board.queue[board.tail];
                        board.tail = (board.tail + 1) % EVENT_QUEUE_LEN;
                }

                size_t lost = board.lost;
                board.lost = 0;
                bool stop = board.stop;
                board.uart_kick = false;

                if (stop) {
                        board.running = false;
                        if (board.req.pending) {
                                board.req.result = _HOST_BOARD_I2C_ERROR;
                                board.req.pending = false;
                                pthread_cond_broadcast(&board.req_cond);
                        }
                }

                pthread_mutex_unlock(&board.mtx);

                if (stop) {
                        for (size_t i = 0; i < n; i++) {
                                free(events[i].text);
                        }

                        call_ref(board.ref_run_exit, 0, 0);

                        pthread_mutex_lock(&board.mtx);
                        board.exit_done = true;
                        pthread_cond_broadcast(&board.cond);
                        pthread_mutex_unlock(&board.mtx);
                        break;
                }

                if (lost) {
                        char msg[64];
                        snprintf(msg, sizeof(msg), "event queue overflow, %zu event(s) lost", lost);
                        board_log(msg);
                }

                for (size_t i = 0; i < n; i++) {
                        switch (events[i].type) {
                        case EVENT__GPIO_OUTPUT:
                                lua_pushinteger(board.L, events[i].port);
                                lua_pushinteger(board.L, events[i].state);
                                lua_pushinteger(board.L, events[i].changed);
                                call_ref(board.ref_gpio_event, 3, 0);
                                break;

                        case EVENT__PANEL_CONNECT:
                        case EVENT__PANEL_MESSAGE:
                        case EVENT__PANEL_DISCONNECT:
                                lua_pushinteger(board.L, events[i].type - EVENT__PANEL_CONNECT);
                                lua_pushstring(board.L, events[i].text ? events[i].text : "");
                                call_ref(board.ref_panel_event, 2, 0);
                                break;

                        case EVENT__UART_CONFIG:
                                lua_pushinteger(board.L, 1);
                                lua_pushinteger(board.L, events[i].port);
                                lua_pushnil(board.L);
                                call_ref(board.ref_uart_event, 3, 0);
                                break;
                        }

                        free(events[i].text);
                }

                /* request after events: events were sent by system before request (events
                 * queued after the queue was read are handled first in the next loop) */
                pthread_mutex_lock(&board.mtx);
                if (board.req.pending && !board.req.taken && (board.head == board.tail)) {
                        handle_request();
                }
                pthread_mutex_unlock(&board.mtx);

                uart_deliver();
                call_ref(board.ref_run_timers, 0, 0);
        }

        return NULL;
}

//==============================================================================
/**
 * @brief  Queue event for board thread. Called with locked mutex. Text of lost
 *         event is freed.
 *
 * @param  ev           event
 */
//==============================================================================
static void queue_event(const event_t *ev)
{
        size_t next = (board.head + 1) % EVENT_QUEUE_LEN;
        if (next != board.tail) {
                board.queue[board.head] = *ev;
                board.head = next;
                pthread_cond_broadcast(&board.cond);
        } else {
                free(ev->text);
                board.lost++;
        }
}

//==============================================================================
/**
 * @brief  Queue panel event (panel thread).
 *
 * @param  type         event type
 * @param  text         message (copied, can be NULL)
 */
//==============================================================================
static void queue_panel_event(event_type_t type, const char *text)
{
        event_t ev = {.type = type, .text = text ? strdup(text) : NULL};

        pthread_mutex_lock(&board.mtx);
        if (board.running) {
                queue_event(&ev);
        } else {
                free(ev.text);
        }
        pthread_mutex_unlock(&board.mtx);
}

//==============================================================================
/**
 * @brief  Panel thread: accepts panel connection (one at a time) and queues
 *         received lines as events of board thread.
 */
//==============================================================================
static void *panel_thread(void *arg)
{
        (void)arg;

        static char line[PANEL_LINE_MAX];

        for (;;) {
                int fd = accept4(board.panel_listen, NULL, NULL, SOCK_CLOEXEC);
                if (fd < 0) {
                        if (errno == EINTR || errno == ECONNABORTED) {
                                continue;
                        }
                        break;
                }

                struct timeval tmo = {.tv_sec = PANEL_SEND_TMO / 1000,
                                      .tv_usec = (PANEL_SEND_TMO % 1000) * 1000};
                setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof(tmo));

                pthread_mutex_lock(&board.mtx);
                board.panel_client = fd;
                board.panel_conn++;
                pthread_mutex_unlock(&board.mtx);

                queue_panel_event(EVENT__PANEL_CONNECT, NULL);

                size_t len = 0;
                bool skip = false; /* too long line is dropped */

                for (;;) {
                        char buf[512];
                        ssize_t r = recv(fd, buf, sizeof(buf), 0);
                        if (r < 0 && errno == EINTR) {
                                continue;
                        }
                        if (r <= 0) {
                                break;
                        }

                        for (ssize_t i = 0; i < r; i++) {
                                char ch = buf[i];
                                if (ch == '\n') {
                                        if (!skip && (len > 0)) {
                                                if (line[len - 1] == '\r') {
                                                        len--;
                                                }
                                                line[len] = '\0';
                                                queue_panel_event(EVENT__PANEL_MESSAGE, line);
                                        }
                                        len = 0;
                                        skip = false;
                                } else if (len < sizeof(line) - 1) {
                                        line[len++] = ch;
                                } else {
                                        skip = true;
                                }
                        }
                }

                pthread_mutex_lock(&board.mtx);
                board.panel_client = -1;
                pthread_mutex_unlock(&board.mtx);

                close(fd);
                queue_panel_event(EVENT__PANEL_DISCONNECT, NULL);
        }

        return NULL;
}

//==============================================================================
/**
 * @brief  Base64 encoding.
 *
 * @param  src          data
 * @param  len          data length
 * @param  dst          destination (4 * ((len + 2) / 3) bytes)
 *
 * @return Length of encoded data.
 */
//==============================================================================
static size_t base64(const uint8_t *src, size_t len, char *dst)
{
        static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        char *p = dst;

        for (size_t i = 0; i < len; i += 3) {
                uint32_t v = (uint32_t)src[i] << 16;
                if (i + 1 < len) v |= (uint32_t)src[i + 1] << 8;
                if (i + 2 < len) v |= src[i + 2];

                *p++ = T[(v >> 18) & 63];
                *p++ = T[(v >> 12) & 63];
                *p++ = (i + 1 < len) ? T[(v >> 6) & 63] : '=';
                *p++ = (i + 2 < len) ? T[v & 63] : '=';
        }

        return (size_t)(p - dst);
}

//==============================================================================
/**
 * @brief  Send changed lines of LCD to the panel (LCD thread).
 *         Message: {"t":"lcd","id":"<id>","w":<width>,"h":<height>,
 *         "fmt":"<format>","bg":[r,g,b],"y":<first line>,"n":<lines>,
 *         "data":"<base64>"}, LCD without framebuffer: "w":0.
 *
 * @param  lcd          LCD slot
 * @param  force        all lines are sent
 *
 * @return false when the message cannot be sent (connection is closed).
 */
//==============================================================================
static bool lcd_send(lcd_t *lcd, bool force)
{
        char head[192];
        int first = -1, last = -1;
        size_t line = 0;

        pthread_mutex_lock(&board.lcd.mtx);

        if (lcd->version != lcd->sent_version) {
                lcd->sent_version = lcd->version;
                force = true;
        }

        if (lcd->id[0] == '\0') {
                pthread_mutex_unlock(&board.lcd.mtx);
                return true;
        }

        if (!lcd->fb) {
                snprintf(head, sizeof(head), "{\"t\":\"lcd\",\"id\":\"%s\",\"w\":0,\"h\":0}", lcd->id);
                pthread_mutex_unlock(&board.lcd.mtx);

                if (force || !lcd->sent_empty) {
                        const char *msg = head;
                        size_t len = strlen(msg);
                        lcd->sent_empty = panel_write(&msg, &len, 1);
                        return lcd->sent_empty;
                }

                return true;
        }

        lcd->sent_empty = false;

        int fmt = lcd->format;
        line = (size_t)lcd->width * LCD_PIXEL_SIZE[fmt];
        size_t size = line * lcd->height;

        if (size != lcd->shadow_size) {
                free(lcd->shadow);
                free(lcd->enc);
                lcd->shadow = malloc(size);
                lcd->enc = malloc(4 * ((size + 2) / 3) + 1);
                lcd->shadow_size = (lcd->shadow && lcd->enc) ? size : 0;
                force = true;
        }

        if (lcd->shadow_size) {
                for (int y = 0; y < lcd->height; y++) {
                        const uint8_t *src = lcd->fb + (size_t)y * line;
                        uint8_t *dst = lcd->shadow + (size_t)y * line;
                        if (force || (memcmp(src, dst, line) != 0)) {
                                memcpy(dst, src, line);
                                if (first < 0) first = y;
                                last = y;
                        }
                }
        }

        snprintf(head, sizeof(head),
                 "{\"t\":\"lcd\",\"id\":\"%s\",\"w\":%u,\"h\":%u,\"fmt\":\"%s\",\"bg\":[%u,%u,%u],"
                 "\"y\":%d,\"n\":%d,\"data\":\"",
                 lcd->id, lcd->width, lcd->height, LCD_FORMAT[fmt], lcd->bg[0], lcd->bg[1], lcd->bg[2], first,
                 last - first + 1);

        pthread_mutex_unlock(&board.lcd.mtx);

        /* shadow is used only by this thread, sending can take time */
        if (first >= 0) {
                size_t len = base64(lcd->shadow + (size_t)first * line, (size_t)(last - first + 1) * line, lcd->enc);
                const char *part[] = {head, lcd->enc, "\"}"};
                const size_t psize[] = {strlen(head), len, 2};
                return panel_write(part, psize, 3);
        }

        return true;
}

//==============================================================================
/**
 * @brief  LCD thread: sends changed lines of LCDs (LTDC driver and LCDs of
 *         board models) to the panel. All lines are sent after connection
 *         and after framebuffer change (attach).
 */
//==============================================================================
static void *lcd_thread(void *arg)
{
        (void)arg;

        unsigned conn = 0;

        for (;;) {
                struct timespec t = {.tv_sec = 0, .tv_nsec = LCD_REFRESH_MS * 1000000L};
                nanosleep(&t, NULL);

                pthread_mutex_lock(&board.mtx);
                bool connected = (board.panel_client >= 0);
                unsigned cur_conn = board.panel_conn;
                pthread_mutex_unlock(&board.mtx);

                if (!connected) {
                        continue;
                }

                bool force = (cur_conn != conn);
                conn = cur_conn;

                for (int i = 0; i < LCD_MAX; i++) {
                        if (!lcd_send(&board.lcd.slot[i], force)) {
                                conn = 0; /* all lines after reconnection */
                                break;
                        }
                }
        }

        return NULL;
}

//==============================================================================
/**
 * @brief  LCD framebuffer of LTDC driver is shown by the panel (LCD "ltdc").
 */
//==============================================================================
void _host_board_lcd_attach(const void *fb, uint16_t width, uint16_t height, int format, uint8_t bg_r,
                            uint8_t bg_g, uint8_t bg_b)
{
        if ((format < 0) || (format >= LCD_FORMATS)) {
                fb = NULL;
        }

        pthread_mutex_lock(&board.lcd.mtx);
        lcd_t *lcd = &board.lcd.slot[0];
        lcd->fb = fb;
        lcd->width = fb ? width : 0;
        lcd->height = fb ? height : 0;
        lcd->format = fb ? format : 0;
        lcd->bg[0] = bg_r;
        lcd->bg[1] = bg_g;
        lcd->bg[2] = bg_b;
        lcd->version++;
        pthread_mutex_unlock(&board.lcd.mtx);
}

//==============================================================================
/**
 * @brief  Create panel socket and start panel thread.
 *
 * @return 0 on success, otherwise -1.
 */
//==============================================================================
static int panel_start(void)
{
        struct sockaddr_un addr = {.sun_family = AF_UNIX};

        if (strlen(board.panel_path) >= sizeof(addr.sun_path)) {
                fprintf(stderr, "board: panel socket path is too long: %s\n", board.panel_path);
                return -1;
        }

        strcpy(addr.sun_path, board.panel_path);

        board.panel_listen = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (board.panel_listen < 0) {
                perror("board: panel socket");
                return -1;
        }

        unlink(board.panel_path);

        if (  (bind(board.panel_listen, (struct sockaddr *)&addr, sizeof(addr)) != 0)
           || (listen(board.panel_listen, 1) != 0) ) {
                fprintf(stderr, "board: panel socket %s: %s\n", board.panel_path, strerror(errno));
                close(board.panel_listen);
                board.panel_listen = -1;
                return -1;
        }

        int err = pthread_create(&board.panel_thread, NULL, panel_thread, NULL);
        if (err) {
                fprintf(stderr, "board: unable to create panel thread: %s\n", strerror(err));
                close(board.panel_listen);
                board.panel_listen = -1;
                unlink(board.panel_path);
                return -1;
        }

        err = pthread_create(&board.lcd.thread, NULL, lcd_thread, NULL);
        if (err) {
                fprintf(stderr, "board: unable to create LCD thread: %s\n", strerror(err));
        }

        board.panel_started = true;
        return 0;
}

//==============================================================================
/**
 * @brief  Load board description and start board model.
 *
 * @param  script       board description file
 *
 * @return 0 on success, otherwise -1.
 */
//==============================================================================
int _host_board_init(const char *script, const char *panel)
{
        board.script = script;
        board.panel_path = panel;
        clock_gettime(CLOCK_MONOTONIC, &board.t0);

        /* board thread does not handle any signal (scheduler signals are sent to tasks) */
        sigset_t all, old;
        sigfillset(&all);
        pthread_sigmask(SIG_SETMASK, &all, &old);
        int err = pthread_create(&board.thread, NULL, board_thread, NULL);
        pthread_sigmask(SIG_SETMASK, &old, NULL);

        if (err) {
                fprintf(stderr, "board: unable to create thread: %s\n", strerror(err));
                return -1;
        }

        pthread_mutex_lock(&board.mtx);
        while (!board.init_done) {
                pthread_cond_wait(&board.cond, &board.mtx);
        }
        int result = board.init_result;
        pthread_mutex_unlock(&board.mtx);

        if (result != 0) {
                pthread_join(board.thread, NULL);
                fprintf(stderr, "board: unable to load %s\n", script);

        } else if (panel) {
                pthread_sigmask(SIG_SETMASK, &all, &old);
                result = panel_start();
                pthread_sigmask(SIG_SETMASK, &old, NULL);

                if (result != 0) {
                        _host_board_exit();
                }
        }

        return result;
}

//==============================================================================
/**
 * @brief  Stop board model.
 */
//==============================================================================
void _host_board_exit(void)
{
        pthread_mutex_lock(&board.mtx);

        if (board.init_done && (board.init_result == 0) && !board.stop) {
                board.stop = true;
                board.running = false;
                pthread_cond_broadcast(&board.cond);

                /* wait for exit handlers of board */
                struct timespec t;
                clock_gettime(CLOCK_REALTIME, &t);
                t.tv_sec += 2;

                while (!board.exit_done) {
                        if (pthread_cond_timedwait(&board.cond, &board.mtx, &t) == ETIMEDOUT) {
                                break;
                        }
                }
        }

        int client = board.panel_client;

        pthread_mutex_unlock(&board.mtx);

        /* panel sees end of connection, socket file is removed */
        if (board.panel_started) {
                if (client >= 0) {
                        shutdown(client, SHUT_RDWR);
                }
                unlink(board.panel_path);
        }
}

//==============================================================================
/**
 * @brief  GPIO output state was changed by system (event is queued).
 *
 * @param  port         port index
 * @param  state        port output state
 * @param  changed      mask of changed pins
 */
//==============================================================================
void _host_board_gpio_output(int port, uint32_t state, uint32_t changed)
{
        if ((port < 0) || (port >= GPIO_PORTS)) {
                return;
        }

        pthread_mutex_lock(&board.mtx);

        board.out_state[port] = state;

        queue_event(&(event_t){
                .type = EVENT__GPIO_OUTPUT,
                .port = port,
                .state = state,
                .changed = changed,
        });

        pthread_mutex_unlock(&board.mtx);
}

//==============================================================================
/**
 * @brief  Get state of GPIO pins driven by board.
 *
 * @param  port         port index
 * @param  level        levels of driven pins
 *
 * @return Mask of pins driven by board.
 */
//==============================================================================
uint32_t _host_board_gpio_input(int port, uint32_t *level)
{
        if ((port < 0) || (port >= GPIO_PORTS)) {
                *level = 0;
                return 0;
        }

        pthread_mutex_lock(&board.mtx);
        uint32_t mask = board.drive_mask[port];
        *level = board.drive_level[port] & mask;
        pthread_mutex_unlock(&board.mtx);

        return mask;
}

//==============================================================================
/**
 * @brief  Send request to board thread and wait for result.
 *
 * @param  req          request (input fields)
 *
 * @return Request result.
 */
//==============================================================================
static int request(request_t *req)
{
        int result = _HOST_BOARD_I2C_ERROR;
        bool timeout = false;

        pthread_mutex_lock(&board.req_mtx);
        pthread_mutex_lock(&board.mtx);

        if (board.running) {
                uint32_t seq = board.req.seq + 1;
                board.req = *req;
                board.req.seq = seq;
                board.req.pending = true;
                board.req.taken = false;
                pthread_cond_broadcast(&board.cond);

                struct timespec t;
                clock_gettime(CLOCK_REALTIME, &t);
                t.tv_sec += REQUEST_TIMEOUT / 1000;
                t.tv_nsec += (REQUEST_TIMEOUT % 1000) * 1000000L;
                if (t.tv_nsec >= 1000000000L) {
                        t.tv_sec++;
                        t.tv_nsec -= 1000000000L;
                }

                while (board.req.pending) {
                        if (pthread_cond_timedwait(&board.req_cond, &board.mtx, &t) == ETIMEDOUT) {
                                if (board.req.pending) {
                                        board.req.pending = false;
                                        timeout = true;
                                }
                                break;
                        }
                }

                if (!timeout) {
                        result = board.req.result;
                        req->done = board.req.done;
                }
        }

        pthread_mutex_unlock(&board.mtx);
        pthread_mutex_unlock(&board.req_mtx);

        if (timeout) {
                board_log("request timeout (board does not respond)");
        }

        return result;
}

//==============================================================================
/**
 * @brief  SPI master: full duplex transfer.
 */
//==============================================================================
int _host_board_spi_transfer(int bus, const uint8_t *tx, uint8_t *rx, size_t count, uint8_t fill, unsigned flags)
{
        if ((bus < 0) || (bus >= SPI_BUSES) || (count == 0)) {
                if (rx) {
                        memset(rx, 0xFF, count);
                }
                return (count == 0) ? 0 : -1;
        }

        request_t req = {.type = REQUEST__SPI_TRANSFER, .bus = bus, .src = tx, .dst = rx, .len = count,
                         .fill = fill, .flags = flags};
        return request(&req);
}

//==============================================================================
/**
 * @brief  I2C master: START or repeated START condition.
 */
//==============================================================================
int _host_board_i2c_start(int bus)
{
        if ((bus < 0) || (bus >= I2C_BUSES)) {
                return _HOST_BOARD_I2C_ERROR;
        }

        request_t req = {.type = REQUEST__I2C_START, .bus = bus};
        return request(&req);
}

//==============================================================================
/**
 * @brief  I2C master: send device address.
 */
//==============================================================================
int _host_board_i2c_address(int bus, uint16_t address, bool read)
{
        if ((bus < 0) || (bus >= I2C_BUSES)) {
                return _HOST_BOARD_I2C_NACK;
        }

        request_t req = {.type = REQUEST__I2C_ADDRESS, .bus = bus, .address = address, .read = read};
        return request(&req);
}

//==============================================================================
/**
 * @brief  I2C master: transmit bytes to selected device.
 */
//==============================================================================
int _host_board_i2c_write(int bus, const uint8_t *src, size_t count, size_t *written)
{
        *written = 0;

        if ((bus < 0) || (bus >= I2C_BUSES)) {
                return _HOST_BOARD_I2C_NACK;
        }

        request_t req = {.type = REQUEST__I2C_WRITE, .bus = bus, .src = src, .len = count};
        int result = request(&req);
        if (result != _HOST_BOARD_I2C_ERROR) {
                *written = req.done;
        }

        return result;
}

//==============================================================================
/**
 * @brief  I2C master: receive bytes from selected device.
 */
//==============================================================================
int _host_board_i2c_read(int bus, uint8_t *dst, size_t count)
{
        if ((bus < 0) || (bus >= I2C_BUSES)) {
                memset(dst, 0xFF, count);
                return _HOST_BOARD_I2C_NACK;
        }

        request_t req = {.type = REQUEST__I2C_READ, .bus = bus, .dst = dst, .len = count};
        return request(&req);
}

//==============================================================================
/**
 * @brief  I2C master: STOP condition.
 */
//==============================================================================
void _host_board_i2c_stop(int bus)
{
        if ((bus >= 0) && (bus < I2C_BUSES)) {
                request_t req = {.type = REQUEST__I2C_STOP, .bus = bus};
                request(&req);
        }
}

//==============================================================================
/**
 * @brief  Allow connection of UART to board model.
 *
 * @param  port         UART index
 * @param  allowed      true: board model can be connected
 */
//==============================================================================
void _host_board_uart_allow(int port, bool allowed)
{
        if ((port >= 0) && (port < UART_PORTS)) {
                pthread_mutex_lock(&board.uart_mtx);
                board.uart[port].allowed = allowed;
                uart_set_config(&board.uart[port], 115200, 0, 1);
                pthread_mutex_unlock(&board.uart_mtx);
        }
}

//==============================================================================
/**
 * @brief  Check if UART is connected to board model.
 *
 * @param  port         UART index
 *
 * @return true if board model is attached to UART.
 */
//==============================================================================
bool _host_board_uart_attached(int port)
{
        bool attached = false;

        if ((port >= 0) && (port < UART_PORTS)) {
                pthread_mutex_lock(&board.uart_mtx);
                attached = board.uart[port].attached;
                pthread_mutex_unlock(&board.uart_mtx);
        }

        return attached;
}

//==============================================================================
/**
 * @brief  UART configuration changed by system (model is notified).
 *
 * @param  port         UART index
 * @param  baud         baud rate
 * @param  parity       0: none, 1: odd, 2: even
 * @param  stop_bits    1 or 2
 *
 * @return 0 on success, otherwise errno value.
 */
//==============================================================================
int _host_board_uart_configure(int port, uint32_t baud, int parity, int stop_bits)
{
        if ((port < 0) || (port >= UART_PORTS) || (baud == 0) || (parity < 0) || (parity > 2) || (stop_bits < 1) ||
                (stop_bits > 2)) {
                return EINVAL;
        }

        pthread_mutex_lock(&board.uart_mtx);
        uart_set_config(&board.uart[port], baud, parity, stop_bits);
        bool attached = board.uart[port].attached;
        pthread_mutex_unlock(&board.uart_mtx);

        if (attached) {
                event_t ev = {.type = EVENT__UART_CONFIG, .port = port};
                pthread_mutex_lock(&board.mtx);
                if (board.running) {
                        queue_event(&ev);
                }
                pthread_mutex_unlock(&board.mtx);
        }

        return 0;
}

//==============================================================================
/**
 * @brief  UART: data received by system (sent by board model).
 *
 * @param  port         UART index
 * @param  buf          buffer
 * @param  len          buffer length
 *
 * @return Number of read bytes (0: no data).
 */
//==============================================================================
int _host_board_uart_read(int port, void *buf, size_t len)
{
        if ((port < 0) || (port >= UART_PORTS)) {
                return 0;
        }

        pthread_mutex_lock(&board.uart_mtx);
        uart_port_t *u = &board.uart[port];
        double now = time_ms();
        uart_overrun(u, now);
        size_t n = fifo_get(&u->rx, buf, len, now);
        if (n > 0) {
                u->overrun = 0;
                u->overrun_reported = false;
        }
        pthread_mutex_unlock(&board.uart_mtx);

        return (int)n;
}

//==============================================================================
/**
 * @brief  UART: data transmitted by system (received by board model).
 *
 * @param  port         UART index
 * @param  buf          data
 * @param  len          number of bytes
 *
 * @return Number of accepted bytes.
 */
//==============================================================================
int _host_board_uart_write(int port, const void *buf, size_t len)
{
        if ((port < 0) || (port >= UART_PORTS)) {
                return (int)len;
        }

        const uint8_t *src = buf;
        size_t n = 0;

        pthread_mutex_lock(&board.uart_mtx);
        uart_port_t *u = &board.uart[port];
        if (!u->attached) {
                n = len; /* not connected: data is dropped */
        } else {
                double now = time_ms();
                if (u->tx_line < now) {
                        u->tx_line = now;
                }
                for (; n < len; n++) {
                        if (u->timing && (u->tx_line >= now + UART_TX_WINDOW)) {
                                break; /* transmitter busy */
                        }
                        double at = u->timing ? (u->tx_line + u->char_ms) : now;
                        if (!fifo_put(&u->tx, src[n], at)) {
                                break;
                        }
                        if (u->timing) {
                                u->tx_line = at;
                        }
                }
        }
        bool kick = u->attached && (n > 0);
        pthread_mutex_unlock(&board.uart_mtx);

        if (kick) {
                pthread_mutex_lock(&board.mtx);
                board.uart_kick = true;
                pthread_cond_broadcast(&board.cond);
                pthread_mutex_unlock(&board.mtx);
        }

        return (int)n;
}

/*==============================================================================
  End of file
==============================================================================*/
