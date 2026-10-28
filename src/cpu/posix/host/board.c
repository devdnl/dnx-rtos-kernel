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
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

/*==============================================================================
  Local object types
==============================================================================*/
typedef enum {
        EVENT__GPIO_OUTPUT,
} event_type_t;

typedef struct {
        event_type_t type;
        int port;
        uint32_t state;
        uint32_t changed;
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
};

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
        "-- drive input pin of the system (true/1: high, false/0: low)\n"
        "function gpio.set(name, v) local p, n = pin(name); B.drive(p, n, level(v)) end\n"
        "\n"
        "-- stop driving pin (system sees its own output state)\n"
        "function gpio.release(name) local p, n = pin(name); B.drive(p, n, nil) end\n"
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
        "        end\n"
        "    end\n"
        "end\n"
        "\n"
        "-- i2c -------------------------------------------------------------------\n"
        "i2c = {}\n"
        "\n"
        "local buses = {}\n"
        "\n"
        "-- bus name in STM32 style: I2C1 (major 0) ..\n"
        "local function bus_index(name)\n"
        "    local n = tonumber(string.match(tostring(name), '^I2C(%d+)$'))\n"
        "    if not n or n < 1 or n > B.I2C_BUSES then\n"
        "        error('invalid I2C bus name: ' .. tostring(name), 3)\n"
        "    end\n"
        "    return n - 1\n"
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
        "-- bus name in STM32 style: SPI1 (major 0) ..\n"
        "local function spi_bus_index(name)\n"
        "    local n = tonumber(string.match(tostring(name), '^SPI(%d+)$'))\n"
        "    if not n or n < 1 or n > B.SPI_BUSES then\n"
        "        error('invalid SPI bus name: ' .. tostring(name), 3)\n"
        "    end\n"
        "    return n - 1\n"
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
        "                    print(string.format('SPI%d: device does not support mode %d: no response',\n"
        "                                        idx + 1, mode))\n"
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
        "return next_timeout, run_timers, gpio_event, i2c_request, run_exit, spi_request\n";

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
        lua_pushinteger(L, _HOST_BOARD_SPI_LSB_FIRST);
        lua_setfield(L, -2, "SPI_LSB_FIRST");
        lua_pushinteger(L, _HOST_BOARD_I2C_ADDR_10BIT);
        lua_setfield(L, -2, "I2C_ADDR_10BIT");

        if (lua_pcall(L, 1, 6, 1) != LUA_OK) {
                board_log(lua_tostring(L, -1));
                return false;
        }

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

                pthread_mutex_lock(&board.mtx);

                bool request = board.req.pending && !board.req.taken;

                if (!board.stop && (board.head == board.tail) && !request) {
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
                        if (events[i].type == EVENT__GPIO_OUTPUT) {
                                lua_pushinteger(board.L, events[i].port);
                                lua_pushinteger(board.L, events[i].state);
                                lua_pushinteger(board.L, events[i].changed);
                                call_ref(board.ref_gpio_event, 3, 0);
                        }
                }

                /* request after events: events were sent by system before request (events
                 * queued after the queue was read are handled first in the next loop) */
                pthread_mutex_lock(&board.mtx);
                if (board.req.pending && !board.req.taken && (board.head == board.tail)) {
                        handle_request();
                }
                pthread_mutex_unlock(&board.mtx);

                call_ref(board.ref_run_timers, 0, 0);
        }

        return NULL;
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
int _host_board_init(const char *script)
{
        board.script = script;
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

        pthread_mutex_unlock(&board.mtx);
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

        size_t next = (board.head + 1) % EVENT_QUEUE_LEN;
        if (next != board.tail) {
                board.queue[board.head] = (event_t){
                        .type = EVENT__GPIO_OUTPUT,
                        .port = port,
                        .state = state,
                        .changed = changed,
                };
                board.head = next;
                pthread_cond_broadcast(&board.cond);
        } else {
                board.lost++;
        }

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

/*==============================================================================
  End of file
==============================================================================*/
