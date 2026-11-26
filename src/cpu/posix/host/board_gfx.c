/*==============================================================================
@file    board_gfx.c

@author  Daniel Zorychta

@brief   Graphics helpers of board models (POSIX simulator). Models of
         display controllers keep their memory in a memory buffer
         (gfx.memory) and draw frames to a canvas (gfx.canvas): framebuffer
         of 0xRRGGBB pixels with tag buffer (touch tags of EVE controllers),
         scissor and alpha blending. Pixel loops are implemented in C, the
         model interprets commands in Lua. The canvas is copied to an LCD of
         the board panel by canvas:push(lcd) (only changed rows).

@note    Copyright (C) 2026 Daniel Zorychta

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

/* pixel loops: optimized for speed (the simulator is built for size) */
#pragma GCC optimize("O2")

/*==============================================================================
  Include files
==============================================================================*/
#include <math.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lua/lua.h"
#include "lua/lauxlib.h"

#include "board_gfx.h"

/*==============================================================================
  Local macros
==============================================================================*/
#define MEMORY_METATABLE   "board.gfx.memory"
#define CANVAS_METATABLE   "board.gfx.canvas"
#define INFLATER_METATABLE "board.gfx.inflater"
#define MEMORY_SIZE_MAX    (64 * 1024 * 1024)
#define CANVAS_SIZE_MAX    4096

/* bitmap formats (EVE: BITMAP_LAYOUT) */
#define FMT_ARGB1555     0
#define FMT_L1           1
#define FMT_L4           2
#define FMT_L8           3
#define FMT_RGB332       4
#define FMT_ARGB2        5
#define FMT_ARGB4        6
#define FMT_RGB565       7
#define FMT_PALETTED     8
#define FMT_PALETTED565  14
#define FMT_PALETTED4444 15
#define FMT_PALETTED8    16
#define FMT_L2           17

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/*==============================================================================
  Local types
==============================================================================*/
typedef struct {
        size_t size;
        uint8_t *data;
} memory_t;

typedef struct {
        int width;
        int height;
        uint32_t *px;       /* 0xRRGGBB */
        uint8_t *tags;      /* tag buffer */
        int sx, sy, sw, sh; /* scissor */
        uint8_t tag;        /* current tag */
        bool tag_mask;      /* tags are written */
} canvas_t;

/* zlib stream decoder (RFC 1950/1951) resumed at block boundaries */
typedef enum {
        INF_HEADER,  /* zlib header */
        INF_BLOCKS,  /* deflate blocks */
        INF_TRAILER, /* Adler-32 */
        INF_DONE,
        INF_ERROR,
} inf_phase_t;

typedef struct {
        size_t in;       /* position in input buffer */
        uint32_t bitbuf; /* bits not used yet */
        int bitcnt;
        size_t out; /* number of output bytes */
        inf_phase_t phase;
} inf_state_t;

typedef struct {
        uint8_t *in; /* input bytes not yet consumed by the checkpoint */
        size_t in_len;
        size_t in_cap;
        size_t in_base; /* bytes dropped from the input buffer */
        int64_t dst;    /* output address in memory */
        inf_state_t cp; /* state at the beginning of the current block */
        inf_state_t st; /* working state */
        bool adler_ok;
        const char *err;
        jmp_buf jmp; /* end of input or error while decoding */
} inflater_t;

typedef struct {
        uint16_t count[16];   /* number of codes of each length */
        uint16_t symbol[288]; /* symbols ordered by code */
} huff_t;

/* drawing area: scissor and canvas, inclusive */
typedef struct {
        int x0, y0, x1, y1;
} clip_t;

/*==============================================================================
  Local objects
==============================================================================*/
static uint32_t crc_table[256];

/*==============================================================================
  Function definitions
==============================================================================*/

//==============================================================================
/**
 * @brief  Memory object at stack index.
 */
//==============================================================================
static memory_t *check_memory(lua_State *L, int idx)
{
        return luaL_checkudata(L, idx, MEMORY_METATABLE);
}

//==============================================================================
/**
 * @brief  Lua: gfx.memory(size) -> memory object (bytes are 0)
 */
//==============================================================================
static int l_memory_new(lua_State *L)
{
        lua_Integer size = luaL_checkinteger(L, 1);
        luaL_argcheck(L, (size > 0) && (size <= MEMORY_SIZE_MAX), 1, "invalid size");

        memory_t *m = lua_newuserdatauv(L, sizeof(memory_t), 0);
        m->size = 0;
        m->data = calloc((size_t)size, 1);
        luaL_setmetatable(L, MEMORY_METATABLE);

        if (!m->data) {
                return luaL_error(L, "gfx.memory: not enough memory");
        }

        m->size = (size_t)size;
        return 1;
}

//==============================================================================
/**
 * @brief  Range of memory: [addr, addr + n) clipped to memory.
 *
 * @return Number of bytes in memory (0: out of memory).
 */
//==============================================================================
static size_t memory_range(const memory_t *m, lua_Integer addr, lua_Integer n)
{
        if ((addr < 0) || (n <= 0) || ((size_t)addr >= m->size)) {
                return 0;
        }

        return ((size_t)n < m->size - (size_t)addr) ? (size_t)n : m->size - (size_t)addr;
}

//==============================================================================
/**
 * @brief  Lua: mem:get(addr) -> byte (0 out of memory)
 */
//==============================================================================
static int l_memory_get(lua_State *L)
{
        memory_t *m = check_memory(L, 1);
        lua_Integer a = luaL_checkinteger(L, 2);
        lua_pushinteger(L, ((a >= 0) && ((size_t)a < m->size)) ? m->data[a] : 0);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: mem:set(addr, byte) (ignored out of memory)
 */
//==============================================================================
static int l_memory_set(lua_State *L)
{
        memory_t *m = check_memory(L, 1);
        lua_Integer a = luaL_checkinteger(L, 2);
        lua_Integer v = luaL_checkinteger(L, 3);

        if ((a >= 0) && ((size_t)a < m->size)) {
                m->data[a] = (uint8_t)v;
        }

        return 0;
}

//==============================================================================
/**
 * @brief  Lua: mem:read(addr, n) -> string of n bytes (0 out of memory)
 */
//==============================================================================
static int l_memory_read(lua_State *L)
{
        memory_t *m = check_memory(L, 1);
        lua_Integer a = luaL_checkinteger(L, 2);
        lua_Integer n = luaL_checkinteger(L, 3);

        if (n <= 0) {
                lua_pushliteral(L, "");
                return 1;
        }

        luaL_Buffer b;
        char *p = luaL_buffinitsize(L, &b, (size_t)n);
        memset(p, 0, (size_t)n);

        lua_Integer skip = (a < 0) ? -a : 0;
        if (skip < n) {
                size_t k = memory_range(m, a + skip, n - skip);
                if (k) {
                        memcpy(p + skip, m->data + a + skip, k);
                }
        }

        luaL_pushresultsize(&b, (size_t)n);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: mem:write(addr, data) -> number of written bytes (data out of
 *         memory are ignored)
 */
//==============================================================================
static int l_memory_write(lua_State *L)
{
        memory_t *m = check_memory(L, 1);
        lua_Integer a = luaL_checkinteger(L, 2);
        size_t len;
        const char *d = luaL_checklstring(L, 3, &len);

        size_t k = memory_range(m, a, (lua_Integer)len);
        if (k) {
                memcpy(m->data + a, d, k);
        }

        lua_pushinteger(L, (lua_Integer)k);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: mem:fill(addr, n [, byte])
 */
//==============================================================================
static int l_memory_fill(lua_State *L)
{
        memory_t *m = check_memory(L, 1);
        lua_Integer a = luaL_checkinteger(L, 2);
        lua_Integer n = luaL_checkinteger(L, 3);
        lua_Integer v = luaL_optinteger(L, 4, 0);

        size_t k = memory_range(m, a, n);
        if (k) {
                memset(m->data + a, (int)(v & 0xFF), k);
        }

        return 0;
}

//==============================================================================
/**
 * @brief  Lua: mem:copy(dst, src, n) (overlapping ranges are allowed, bytes
 *         out of memory are read as 0)
 */
//==============================================================================
static int l_memory_copy(lua_State *L)
{
        memory_t *m = check_memory(L, 1);
        lua_Integer dst = luaL_checkinteger(L, 2);
        lua_Integer src = luaL_checkinteger(L, 3);
        lua_Integer n = luaL_checkinteger(L, 4);

        size_t kd = memory_range(m, dst, n);
        if (kd == 0) {
                return 0;
        }

        size_t ks = memory_range(m, src, (lua_Integer)kd);
        if (ks) {
                memmove(m->data + dst, m->data + src, ks);
        }

        if (ks < kd) {
                memset(m->data + dst + ks, 0, kd - ks);
        }

        return 0;
}

//==============================================================================
/**
 * @brief  Lua: mem:crc32(addr, n) -> standard CRC-32 (bytes out of memory: 0)
 */
//==============================================================================
static int l_memory_crc32(lua_State *L)
{
        memory_t *m = check_memory(L, 1);
        lua_Integer a = luaL_checkinteger(L, 2);
        lua_Integer n = luaL_checkinteger(L, 3);

        if (crc_table[1] == 0) {
                for (uint32_t i = 0; i < 256; i++) {
                        uint32_t c = i;
                        for (int k = 0; k < 8; k++) {
                                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                        }
                        crc_table[i] = c;
                }
        }

        uint32_t crc = 0xFFFFFFFFu;
        for (lua_Integer i = 0; i < n; i++) {
                lua_Integer p = a + i;
                uint8_t b = ((p >= 0) && ((size_t)p < m->size)) ? m->data[p] : 0;
                crc = (crc >> 8) ^ crc_table[(crc ^ b) & 0xFF];
        }

        lua_pushinteger(L, (lua_Integer)(~crc & 0xFFFFFFFFu));
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: #mem -> size
 */
//==============================================================================
static int l_memory_len(lua_State *L)
{
        lua_pushinteger(L, (lua_Integer)check_memory(L, 1)->size);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: memory object is collected.
 */
//==============================================================================
static int l_memory_gc(lua_State *L)
{
        memory_t *m = check_memory(L, 1);
        free(m->data);
        m->data = NULL;
        m->size = 0;
        return 0;
}

//==============================================================================
/**
 * @brief  Canvas object at stack index.
 */
//==============================================================================
static canvas_t *check_canvas(lua_State *L, int idx)
{
        canvas_t *c = luaL_checkudata(L, idx, CANVAS_METATABLE);
        luaL_argcheck(L, c->px != NULL, idx, "canvas is released");
        return c;
}

//==============================================================================
/**
 * @brief  Lua: gfx.canvas(width, height) -> canvas (black, tags 0, scissor
 *         2048x2048 at 0, 0, tag 255, tag mask 1)
 */
//==============================================================================
static int l_canvas_new(lua_State *L)
{
        lua_Integer w = luaL_checkinteger(L, 1);
        lua_Integer h = luaL_checkinteger(L, 2);
        luaL_argcheck(L, (w > 0) && (w <= CANVAS_SIZE_MAX), 1, "invalid width");
        luaL_argcheck(L, (h > 0) && (h <= CANVAS_SIZE_MAX), 2, "invalid height");

        canvas_t *c = lua_newuserdatauv(L, sizeof(canvas_t), 0);
        memset(c, 0, sizeof(*c));
        luaL_setmetatable(L, CANVAS_METATABLE);

        c->px = calloc((size_t)(w * h), sizeof(uint32_t));
        c->tags = calloc((size_t)(w * h), 1);

        if (!c->px || !c->tags) {
                free(c->px);
                free(c->tags);
                c->px = NULL;
                c->tags = NULL;
                return luaL_error(L, "gfx.canvas: not enough memory");
        }

        c->width = (int)w;
        c->height = (int)h;
        c->sw = 2048;
        c->sh = 2048;
        c->tag = 255;
        c->tag_mask = true;
        return 1;
}

//==============================================================================
/**
 * @brief  Drawing area: scissor and canvas.
 */
//==============================================================================
static clip_t canvas_clip(const canvas_t *c)
{
        clip_t r;
        r.x0 = (c->sx > 0) ? c->sx : 0;
        r.y0 = (c->sy > 0) ? c->sy : 0;
        r.x1 = c->sx + c->sw - 1;
        r.y1 = c->sy + c->sh - 1;

        if (r.x1 > c->width - 1) {
                r.x1 = c->width - 1;
        }

        if (r.y1 > c->height - 1) {
                r.y1 = c->height - 1;
        }

        return r;
}

//==============================================================================
/**
 * @brief  Blend color over pixel (0 < a < 255).
 */
//==============================================================================
static inline uint32_t blend(uint32_t d, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
        uint32_t na = 255 - a;
        return RGB((r * a + ((d >> 16) & 0xFF) * na) / 255, (g * a + ((d >> 8) & 0xFF) * na) / 255,
                (b * a + (d & 0xFF) * na) / 255);
}

//==============================================================================
/**
 * @brief  Tags of clipped rectangle (current tag when tag mask is set).
 */
//==============================================================================
static void rect_tags(canvas_t *c, int x0, int y0, int x1, int y1)
{
        if (!c->tag_mask) {
                return;
        }

        for (int y = y0; y <= y1; y++) {
                memset(&c->tags[(size_t)y * c->width + x0], c->tag, (size_t)(x1 - x0 + 1));
        }
}

//==============================================================================
/**
 * @brief  Filled clipped rectangle of color (tags written).
 */
//==============================================================================
static void fill_clipped(canvas_t *c, int x0, int y0, int x1, int y1, uint32_t color, uint32_t a)
{
        rect_tags(c, x0, y0, x1, y1);

        if (a == 0) {
                return;
        }

        uint32_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;

        for (int y = y0; y <= y1; y++) {
                uint32_t *p = &c->px[(size_t)y * c->width];
                if (a >= 255) {
                        for (int x = x0; x <= x1; x++) {
                                p[x] = color;
                        }
                } else {
                        for (int x = x0; x <= x1; x++) {
                                p[x] = blend(p[x], r, g, b, a);
                        }
                }
        }
}

//==============================================================================
/**
 * @brief  Filled rectangle, corners in pixels (rounded, inclusive).
 */
//==============================================================================
static void fill_rect(canvas_t *c, double fx0, double fy0, double fx1, double fy1, uint32_t color, uint32_t a)
{
        if (fx1 < fx0) {
                double t = fx0;
                fx0 = fx1;
                fx1 = t;
        }

        if (fy1 < fy0) {
                double t = fy0;
                fy0 = fy1;
                fy1 = t;
        }

        clip_t cl = canvas_clip(c);
        double x0 = floor(fx0 + 0.5), x1 = floor(fx1 + 0.5);
        double y0 = floor(fy0 + 0.5), y1 = floor(fy1 + 0.5);

        if (x0 < cl.x0) {
                x0 = cl.x0;
        }

        if (y0 < cl.y0) {
                y0 = cl.y0;
        }

        if (x1 > cl.x1) {
                x1 = cl.x1;
        }

        if (y1 > cl.y1) {
                y1 = cl.y1;
        }

        if ((x0 <= x1) && (y0 <= y1)) {
                fill_clipped(c, (int)x0, (int)y0, (int)x1, (int)y1, color, a);
        }
}

//==============================================================================
/**
 * @brief  Lua: cv:scissor(x, y, w, h)
 */
//==============================================================================
static int l_canvas_scissor(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        c->sx = (int)luaL_checkinteger(L, 2);
        c->sy = (int)luaL_checkinteger(L, 3);
        c->sw = (int)luaL_checkinteger(L, 4);
        c->sh = (int)luaL_checkinteger(L, 5);
        return 0;
}

//==============================================================================
/**
 * @brief  Lua: cv:tag(tag, mask) (tag is written by drawing when mask is true)
 */
//==============================================================================
static int l_canvas_tag(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        c->tag = (uint8_t)luaL_checkinteger(L, 2);
        c->tag_mask = lua_toboolean(L, 3);
        return 0;
}

//==============================================================================
/**
 * @brief  Lua: cv:clear(color, tag) (scissor area: color 0xRRGGBB and tag,
 *         nil: not cleared)
 */
//==============================================================================
static int l_canvas_clear(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        bool cc = !lua_isnoneornil(L, 2);
        bool ct = !lua_isnoneornil(L, 3);
        uint32_t color = cc ? (uint32_t)luaL_checkinteger(L, 2) & 0xFFFFFF : 0;
        uint8_t tag = ct ? (uint8_t)luaL_checkinteger(L, 3) : 0;

        clip_t cl = canvas_clip(c);
        if ((cl.x0 > cl.x1) || (cl.y0 > cl.y1)) {
                return 0;
        }

        for (int y = cl.y0; y <= cl.y1; y++) {
                size_t o = (size_t)y * c->width;
                if (cc) {
                        for (int x = cl.x0; x <= cl.x1; x++) {
                                c->px[o + x] = color;
                        }
                }

                if (ct) {
                        memset(&c->tags[o + cl.x0], tag, (size_t)(cl.x1 - cl.x0 + 1));
                }
        }

        return 0;
}

//==============================================================================
/**
 * @brief  Lua: cv:rect(x0, y0, x1, y1, color [, alpha]) (corners in pixels,
 *         rounded, inclusive)
 */
//==============================================================================
static int l_canvas_rect(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        fill_rect(c, luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4), luaL_checknumber(L, 5),
                (uint32_t)luaL_checkinteger(L, 6) & 0xFFFFFF, (uint32_t)luaL_optinteger(L, 7, 255) & 0xFF);
        return 0;
}

//==============================================================================
/**
 * @brief  Pixel of anti-aliased shape: coverage cov (0..1) scales alpha a,
 *         tag is written when at least half of the pixel is covered.
 */
//==============================================================================
static inline void aa_pixel(canvas_t *c, size_t o, double cov, uint32_t color, uint32_t a)
{
        if (cov <= 0) {
                return;
        }

        if ((cov >= 0.5) && c->tag_mask) {
                c->tags[o] = c->tag;
        }

        uint32_t ea = (cov >= 1) ? a : (uint32_t)(a * cov + 0.5);

        if (ea >= 255) {
                c->px[o] = color;
        } else if (ea > 0) {
                c->px[o] = blend(c->px[o], (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, ea);
        }
}

//==============================================================================
/**
 * @brief  Coverage of pixel with distance d from the core of shape of radius r
 *         (edge at distance r has coverage 1/2, linear over one pixel).
 */
//==============================================================================
static inline double aa_cov(double r, double d)
{
        double v = r + 0.5 - d;
        return (v <= 0) ? 0 : ((v >= 1) ? 1 : v);
}

//==============================================================================
/**
 * @brief  Anti-aliased rounded rectangle: points within distance r from core
 *         rectangle ax..bx, ay..by (pixel centers), inner part by spans.
 */
//==============================================================================
static void aa_rrect(canvas_t *c, double ax, double ay, double bx, double by, double r, uint32_t color, uint32_t a)
{
        clip_t cl = canvas_clip(c);
        if ((cl.x0 > cl.x1) || (cl.y0 > cl.y1)) {
                return;
        }

        double ro = r + 0.5, ri = r - 0.5;
        int ya = (int)fmax(ceil(ay - ro), cl.y0), yb = (int)fmin(floor(by + ro), cl.y1);

        for (int y = ya; y <= yb; y++) {
                double dy = fmax(fmax(ay - y, y - by), 0);
                if (dy >= ro) {
                        continue;
                }

                double wo = sqrt(ro * ro - dy * dy);
                int xa = (int)fmax(ceil(ax - wo), cl.x0), xb = (int)fmin(floor(bx + wo), cl.x1);
                int fa = xb + 1, fb = xb; /* fully covered span (empty) */

                if ((ri >= 0) && (dy <= ri)) {
                        double wi = sqrt(ri * ri - dy * dy);
                        fa = (int)fmax(ceil(ax - wi), xa);
                        fb = (int)fmin(floor(bx + wi), xb);
                        if (fa <= fb) {
                                fill_clipped(c, fa, y, fb, y, color, a);
                        } else {
                                fa = xb + 1;
                                fb = xb;
                        }
                }

                size_t o = (size_t)y * c->width;
                for (int x = xa; x <= xb; x++) {
                        if (x == fa) {
                                x = fb;
                                continue;
                        }

                        double dx = fmax(fmax(ax - x, x - bx), 0);
                        aa_pixel(c, o + x, aa_cov(r, sqrt(dx * dx + dy * dy)), color, a);
                }
        }
}

//==============================================================================
/**
 * @brief  Anti-aliased capsule: points within distance r from segment x0, y0 -
 *         x1, y1 (circle for equal ends), every pixel is drawn once.
 */
//==============================================================================
static void aa_capsule(canvas_t *c, double x0, double y0, double x1, double y1, double r, uint32_t color, uint32_t a)
{
        clip_t cl = canvas_clip(c);
        if ((cl.x0 > cl.x1) || (cl.y0 > cl.y1)) {
                return;
        }

        double ro = r + 0.5;
        double vx = x1 - x0, vy = y1 - y0;
        double len2 = vx * vx + vy * vy, len = sqrt(len2);
        double ux = (len > 0) ? vx / len : 0, uy = (len > 0) ? vy / len : 0;

        int ya = (int)fmax(ceil(fmin(y0, y1) - ro), cl.y0);
        int yb = (int)fmin(floor(fmax(y0, y1) + ro), cl.y1);

        for (int y = ya; y <= yb; y++) {
                /* x range of the row: hull of end circles and band along the segment */
                double xl = INFINITY, xr = -INFINITY;
                const double ex[2] = {x0, x1}, ey[2] = {y0, y1};

                for (int k = 0; k < 2; k++) {
                        double dy = y - ey[k];
                        if (fabs(dy) < ro) {
                                double w = sqrt(ro * ro - dy * dy);
                                xl = fmin(xl, ex[k] - w);
                                xr = fmax(xr, ex[k] + w);
                        }
                }

                if (len > 0) {
                        /* t(x) = (x - x0) ux + (y - y0) uy in 0..len, n(x) = (y - y0) ux - (x - x0) uy in -ro..ro */
                        double lo = -INFINITY, hi = INFINITY, py = y - y0;
                        bool ok = true;

                        if (ux != 0) {
                                double p = x0 + (0 - py * uy) / ux, q = x0 + (len - py * uy) / ux;
                                lo = fmax(lo, fmin(p, q));
                                hi = fmin(hi, fmax(p, q));
                        } else if ((py * uy < 0) || (py * uy > len)) {
                                ok = false;
                        }

                        if (uy != 0) {
                                double p = x0 + (py * ux - ro) / uy, q = x0 + (py * ux + ro) / uy;
                                lo = fmax(lo, fmin(p, q));
                                hi = fmin(hi, fmax(p, q));
                        } else if (fabs(py * ux) > ro) {
                                ok = false;
                        }

                        if (ok && (lo <= hi)) {
                                xl = fmin(xl, lo);
                                xr = fmax(xr, hi);
                        }
                }

                if (xl > xr) {
                        continue;
                }

                int xa = (int)fmax(ceil(xl), cl.x0), xb = (int)fmin(floor(xr), cl.x1);
                size_t o = (size_t)y * c->width;

                for (int x = xa; x <= xb; x++) {
                        double px = x - x0, py = y - y0;
                        double t = (len2 > 0) ? (px * vx + py * vy) / len2 : 0;
                        t = (t < 0) ? 0 : ((t > 1) ? 1 : t);
                        double dx = px - t * vx, dy = py - t * vy;
                        aa_pixel(c, o + x, aa_cov(r, sqrt(dx * dx + dy * dy)), color, a);
                }
        }
}

//==============================================================================
/**
 * @brief  Lua: cv:disc(x, y, radius, color [, alpha]) (filled circle,
 *         anti-aliased: the edge at radius has coverage 1/2)
 */
//==============================================================================
static int l_canvas_disc(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        double px = luaL_checknumber(L, 2);
        double py = luaL_checknumber(L, 3);
        double rad = luaL_checknumber(L, 4);
        uint32_t color = (uint32_t)luaL_checkinteger(L, 5) & 0xFFFFFF;
        uint32_t a = (uint32_t)luaL_optinteger(L, 6, 255) & 0xFF;

        aa_capsule(c, px, py, px, py, rad, color, a);
        return 0;
}

//==============================================================================
/**
 * @brief  Lua: cv:line(x0, y0, x1, y1, half, color [, alpha]) (line of width
 *         2 * half pixels with round ends, anti-aliased; every pixel is drawn
 *         once)
 */
//==============================================================================
static int l_canvas_line(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        double x0 = luaL_checknumber(L, 2), y0 = luaL_checknumber(L, 3);
        double x1 = luaL_checknumber(L, 4), y1 = luaL_checknumber(L, 5);
        double half = luaL_checknumber(L, 6);
        uint32_t color = (uint32_t)luaL_checkinteger(L, 7) & 0xFFFFFF;
        uint32_t a = (uint32_t)luaL_optinteger(L, 8, 255) & 0xFF;

        aa_capsule(c, x0, y0, x1, y1, fmax(half, 0.5), color, a);
        return 0;
}

//==============================================================================
/**
 * @brief  Lua: cv:rrect(x0, y0, x1, y1, radius, color [, alpha]) (rectangle
 *         with rounded corners, anti-aliased: corners in pixels as cv:rect,
 *         radius 1 is cv:rect, larger radius extends the rectangle by
 *         radius - 1 and rounds the corners, as RECTS with LINE_WIDTH of EVE)
 */
//==============================================================================
static int l_canvas_rrect(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        double x0 = luaL_checknumber(L, 2), y0 = luaL_checknumber(L, 3);
        double x1 = luaL_checknumber(L, 4), y1 = luaL_checknumber(L, 5);
        double rad = luaL_checknumber(L, 6);
        uint32_t color = (uint32_t)luaL_checkinteger(L, 7) & 0xFFFFFF;
        uint32_t a = (uint32_t)luaL_optinteger(L, 8, 255) & 0xFF;

        if (rad <= 1) {
                fill_rect(c, x0, y0, x1, y1, color, a);
                return 0;
        }

        /* core: edges of the rectangle (cv:rect pixels) moved inside by 1/2 px */
        double ax = fmin(x0, x1) + 0.5, bx = fmax(x0, x1) - 0.5;
        double ay = fmin(y0, y1) + 0.5, by = fmax(y0, y1) - 0.5;

        if (ax > bx) {
                ax = bx = (x0 + x1) / 2;
        }

        if (ay > by) {
                ay = by = (y0 + y1) / 2;
        }

        aa_rrect(c, ax, ay, bx, by, rad, color, a);
        return 0;
}

//==============================================================================
/**
 * @brief  Byte of memory (0 out of memory).
 */
//==============================================================================
static inline uint32_t mem_byte(const memory_t *m, int64_t a)
{
        return ((a >= 0) && ((uint64_t)a < m->size)) ? m->data[a] : 0;
}

//==============================================================================
/**
 * @brief  Expand 5/6-bit color component to 8 bits.
 */
//==============================================================================
static inline uint32_t x5(uint32_t v)
{
        return (v << 3) | (v >> 2);
}

static inline uint32_t x6(uint32_t v)
{
        return (v << 2) | (v >> 4);
}

//==============================================================================
/**
 * @brief  Color of palette entry: 0xAARRGGBB.
 */
//==============================================================================
static uint32_t palette_color(const memory_t *m, int fmt, int64_t pal, uint32_t idx)
{
        if (fmt == FMT_PALETTED8) { /* ARGB8888: B, G, R, A */
                int64_t a = pal + (int64_t)idx * 4;
                return (mem_byte(m, a + 3) << 24) | RGB(mem_byte(m, a + 2), mem_byte(m, a + 1), mem_byte(m, a));
        }

        int64_t a = pal + (int64_t)idx * 2;
        uint32_t wd = mem_byte(m, a) | (mem_byte(m, a + 1) << 8);

        if (fmt == FMT_PALETTED4444) {
                return ((((wd >> 12) & 0xF) * 17) << 24) |
                        RGB(((wd >> 8) & 0xF) * 17, ((wd >> 4) & 0xF) * 17, (wd & 0xF) * 17);
        }

        return 0xFF000000u | RGB(x5((wd >> 11) & 0x1F), x6((wd >> 5) & 0x3F), x5(wd & 0x1F)); /* RGB565 */
}

//==============================================================================
/**
 * @brief  Texel u of bitmap row: 0xAARRGGBB, luminance formats return the
 *         luminance as alpha (color is the current color).
 */
//==============================================================================
static inline uint32_t texel(const memory_t *m, int fmt, int64_t row, int u, int64_t pal)
{
        switch (fmt) {
        case FMT_ARGB1555: {
                uint32_t wd = mem_byte(m, row + u * 2) | (mem_byte(m, row + u * 2 + 1) << 8);
                return ((wd & 0x8000) ? 0xFF000000u : 0) |
                        RGB(x5((wd >> 10) & 0x1F), x5((wd >> 5) & 0x1F), x5(wd & 0x1F));
        }
        case FMT_RGB565: {
                uint32_t wd = mem_byte(m, row + u * 2) | (mem_byte(m, row + u * 2 + 1) << 8);
                return 0xFF000000u | RGB(x5((wd >> 11) & 0x1F), x6((wd >> 5) & 0x3F), x5(wd & 0x1F));
        }
        case FMT_ARGB4: {
                uint32_t wd = mem_byte(m, row + u * 2) | (mem_byte(m, row + u * 2 + 1) << 8);
                return ((((wd >> 12) & 0xF) * 17) << 24) |
                        RGB(((wd >> 8) & 0xF) * 17, ((wd >> 4) & 0xF) * 17, (wd & 0xF) * 17);
        }
        case FMT_ARGB2: {
                uint32_t b0 = mem_byte(m, row + u);
                return ((((b0 >> 6) & 3) * 85) << 24) | RGB(((b0 >> 4) & 3) * 85, ((b0 >> 2) & 3) * 85, (b0 & 3) * 85);
        }
        case FMT_RGB332: {
                uint32_t b0 = mem_byte(m, row + u);
                uint32_t r3 = (b0 >> 5) & 7, g3 = (b0 >> 2) & 7, b2 = b0 & 3;
                return 0xFF000000u | RGB((r3 << 5) | (r3 << 2) | (r3 >> 1), (g3 << 5) | (g3 << 2) | (g3 >> 1), b2 * 85);
        }
        case FMT_L8:
                return mem_byte(m, row + u) << 24;
        case FMT_L4: {
                uint32_t b0 = mem_byte(m, row + (u >> 1));
                return (((u & 1) ? (b0 & 0xF) : ((b0 >> 4) & 0xF)) * 17) << 24;
        }
        case FMT_L2: {
                uint32_t b0 = mem_byte(m, row + (u >> 2));
                return (((b0 >> (6 - 2 * (u & 3))) & 3) * 85) << 24;
        }
        case FMT_L1: {
                uint32_t b0 = mem_byte(m, row + (u >> 3));
                return (((b0 >> (7 - (u & 7))) & 1) * 255) << 24;
        }
        case FMT_PALETTED:
                return palette_color(m, FMT_PALETTED565, pal, mem_byte(m, row + u));
        default: /* PALETTED565/4444/8 */
                return palette_color(m, fmt, pal, mem_byte(m, row + u));
        }
}

//==============================================================================
/**
 * @brief  Lua: cv:bitmap(mem, source, format, stride, width, height, x, y,
 *         color, alpha [, palette]) -> true (false: format is not supported)
 *
 *         Bitmap in EVE format at source address of memory (stride: bytes of
 *         row, height: rows) drawn at x, y. Luminance formats (L1/L2/L4/L8)
 *         are the alpha of color, other formats are multiplied by color; the
 *         alpha is multiplied by alpha. The whole visible bitmap has the tag
 *         (alpha test is not modeled).
 */
//==============================================================================
static int l_canvas_bitmap(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        memory_t *m = check_memory(L, 2);
        int64_t base = luaL_checkinteger(L, 3);
        int fmt = (int)luaL_checkinteger(L, 4);
        int64_t stride = luaL_checkinteger(L, 5);
        int bw = (int)luaL_checkinteger(L, 6);
        int bh = (int)luaL_checkinteger(L, 7);
        int x = (int)floor(luaL_checknumber(L, 8));
        int y = (int)floor(luaL_checknumber(L, 9));
        uint32_t color = (uint32_t)luaL_checkinteger(L, 10) & 0xFFFFFF;
        uint32_t ca = (uint32_t)luaL_checkinteger(L, 11) & 0xFF;
        int64_t pal = luaL_optinteger(L, 12, 0);

        bool supported = ((fmt >= FMT_ARGB1555) && (fmt <= FMT_PALETTED)) ||
                ((fmt >= FMT_PALETTED565) && (fmt <= FMT_L2));
        lua_pushboolean(L, supported);
        if (!supported) {
                return 1;
        }

        /* visible part (scissor, canvas, bitmap) */
        clip_t cl = canvas_clip(c);
        int ua = ((cl.x0 > x) ? cl.x0 : x) - x;
        int ub = ((cl.x1 < x + bw - 1) ? cl.x1 : x + bw - 1) - x;
        int va = ((cl.y0 > y) ? cl.y0 : y) - y;
        int vb = ((cl.y1 < y + bh - 1) ? cl.y1 : y + bh - 1) - y;
        if ((ua > ub) || (va > vb)) {
                return 1;
        }

        rect_tags(c, x + ua, y + va, x + ub, y + vb);

        if (ca == 0) {
                return 1;
        }

        uint32_t fr = (color >> 16) & 0xFF, fg = (color >> 8) & 0xFF, fb = color & 0xFF;
        bool plain = (color == 0xFFFFFF);
        bool lum = (fmt == FMT_L1) || (fmt == FMT_L2) || (fmt == FMT_L4) || (fmt == FMT_L8);

        for (int v = va; v <= vb; v++) {
                int64_t row = base + (int64_t)v * stride;
                uint32_t *p = &c->px[(size_t)(y + v) * c->width + x];

                /* opaque RGB565 without tint in memory: direct conversion */
                if ((fmt == FMT_RGB565) && plain && (ca >= 255) && (row >= 0) &&
                        (row + (int64_t)ub * 2 + 1 < (int64_t)m->size)) {
                        const uint8_t *s = &m->data[row];
                        for (int u = ua; u <= ub; u++) {
                                uint32_t wd = s[u * 2] | ((uint32_t)s[u * 2 + 1] << 8);
                                p[u] = RGB(x5((wd >> 11) & 0x1F), x6((wd >> 5) & 0x3F), x5(wd & 0x1F));
                        }
                        continue;
                }

                for (int u = ua; u <= ub; u++) {
                        uint32_t t = texel(m, fmt, row, u, pal);
                        uint32_t ta = t >> 24;
                        uint32_t r, g, b, a;

                        if (lum) {
                                if (ta == 0) {
                                        continue;
                                }
                                r = fr;
                                g = fg;
                                b = fb;
                                a = (ca >= 255) ? ta : (ta * ca) / 255;
                        } else {
                                r = (t >> 16) & 0xFF;
                                g = (t >> 8) & 0xFF;
                                b = t & 0xFF;
                                if (!plain) {
                                        r = (r * fr) / 255;
                                        g = (g * fg) / 255;
                                        b = (b * fb) / 255;
                                }
                                a = (ta * ca) / 255;
                        }

                        if (a >= 255) {
                                p[u] = RGB(r, g, b);
                        } else if (a > 0) {
                                p[u] = blend(p[u], r, g, b, a);
                        }
                }
        }

        return 1;
}

//==============================================================================
/**
 * @brief  Lua: cv:tag_at(x, y) -> tag (0 out of canvas)
 */
//==============================================================================
static int l_canvas_tag_at(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        lua_Integer x = luaL_checkinteger(L, 2);
        lua_Integer y = luaL_checkinteger(L, 3);
        bool in = (x >= 0) && (y >= 0) && (x < c->width) && (y < c->height);
        lua_pushinteger(L, in ? c->tags[(size_t)y * c->width + (size_t)x] : 0);
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: cv:pixel(x, y) -> 0xRRGGBB (nil out of canvas)
 */
//==============================================================================
static int l_canvas_pixel(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        lua_Integer x = luaL_checkinteger(L, 2);
        lua_Integer y = luaL_checkinteger(L, 3);

        if ((x < 0) || (y < 0) || (x >= c->width) || (y >= c->height)) {
                lua_pushnil(L);
        } else {
                lua_pushinteger(L, c->px[(size_t)y * c->width + (size_t)x]);
        }

        return 1;
}

//==============================================================================
/**
 * @brief  Lua: cv:reset() (black, tags 0, scissor 2048x2048 at 0, 0, tag 255,
 *         tag mask 1)
 */
//==============================================================================
static int l_canvas_reset(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        memset(c->px, 0, (size_t)c->width * c->height * sizeof(uint32_t));
        memset(c->tags, 0, (size_t)c->width * c->height);
        c->sx = 0;
        c->sy = 0;
        c->sw = 2048;
        c->sh = 2048;
        c->tag = 255;
        c->tag_mask = true;
        return 0;
}

//==============================================================================
/**
 * @brief  Lua: cv:push(lcd) -> number of written rows (canvas is copied to LCD
 *         converted to its format, only changed rows; LCD of other size: the
 *         common part)
 */
//==============================================================================
static int l_canvas_push(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        lua_pushinteger(L, board_lcd_write_canvas(L, 2, c->px, c->width, c->height));
        return 1;
}

//==============================================================================
/**
 * @brief  Lua: canvas fields: width, height; methods from the method table.
 */
//==============================================================================
static int l_canvas_index(lua_State *L)
{
        canvas_t *c = check_canvas(L, 1);
        const char *key = luaL_checkstring(L, 2);

        if (strcmp(key, "width") == 0) {
                lua_pushinteger(L, c->width);
        } else if (strcmp(key, "height") == 0) {
                lua_pushinteger(L, c->height);
        } else {
                lua_getfield(L, lua_upvalueindex(1), key);
        }

        return 1;
}

//==============================================================================
/**
 * @brief  Lua: canvas is collected.
 */
//==============================================================================
static int l_canvas_gc(lua_State *L)
{
        canvas_t *c = luaL_checkudata(L, 1, CANVAS_METATABLE);
        free(c->px);
        free(c->tags);
        c->px = NULL;
        c->tags = NULL;
        return 0;
}

//==============================================================================
/**
 * @brief  Inflater object at stack index.
 */
//==============================================================================
static inflater_t *check_inflater(lua_State *L, int idx)
{
        return luaL_checkudata(L, idx, INFLATER_METATABLE);
}

//==============================================================================
/**
 * @brief  Lua: gfx.inflater(addr) -> zlib stream decoder writing to memory
 *         address addr (decoder:feed(mem, data) decodes)
 */
//==============================================================================
static int l_inflater_new(lua_State *L)
{
        lua_Integer dst = luaL_checkinteger(L, 1);

        inflater_t *z = lua_newuserdatauv(L, sizeof(inflater_t), 0);
        memset(z, 0, sizeof(*z));
        luaL_setmetatable(L, INFLATER_METATABLE);
        z->dst = dst;
        z->cp.phase = INF_HEADER;
        return 1;
}

//==============================================================================
/**
 * @brief  Error of stream: decoding is finished.
 */
//==============================================================================
static void inf_error(inflater_t *z, const char *msg)
{
        z->err = msg;
        z->st.phase = INF_ERROR;
        longjmp(z->jmp, 2);
}

//==============================================================================
/**
 * @brief  Next input byte (end of input: back to the checkpoint).
 */
//==============================================================================
static inline uint32_t inf_byte(inflater_t *z)
{
        size_t i = z->st.in - z->in_base;
        if (i >= z->in_len) {
                longjmp(z->jmp, 1);
        }

        z->st.in++;
        return z->in[i];
}

//==============================================================================
/**
 * @brief  n bits of input (LSB first).
 */
//==============================================================================
static inline uint32_t inf_bits(inflater_t *z, int n)
{
        while (z->st.bitcnt < n) {
                z->st.bitbuf |= inf_byte(z) << z->st.bitcnt;
                z->st.bitcnt += 8;
        }

        uint32_t v = z->st.bitbuf & ((1u << n) - 1);
        z->st.bitbuf = (n < 32) ? (z->st.bitbuf >> n) : 0;
        z->st.bitcnt -= n;
        return v;
}

//==============================================================================
/**
 * @brief  Output byte (memory out of range: not written).
 */
//==============================================================================
static inline void inf_put(inflater_t *z, memory_t *m, uint8_t b)
{
        int64_t a = z->dst + (int64_t)z->st.out++;
        if ((a >= 0) && ((uint64_t)a < m->size)) {
                m->data[a] = b;
        }
}

//==============================================================================
/**
 * @brief  Canonical Huffman code of code lengths.
 *
 * @return false: over-subscribed code.
 */
//==============================================================================
static bool inf_huffman(huff_t *h, const uint8_t *lengths, int n)
{
        uint16_t offs[16];

        memset(h->count, 0, sizeof(h->count));
        for (int sym = 0; sym < n; sym++) {
                h->count[lengths[sym]]++;
        }

        int left = 1;
        for (int len = 1; len < 16; len++) {
                left = (left << 1) - h->count[len];
                if (left < 0) {
                        return false;
                }
        }

        offs[1] = 0;
        for (int len = 1; len < 15; len++) {
                offs[len + 1] = offs[len] + h->count[len];
        }

        for (int sym = 0; sym < n; sym++) {
                if (lengths[sym]) {
                        h->symbol[offs[lengths[sym]]++] = (uint16_t)sym;
                }
        }

        return true;
}

//==============================================================================
/**
 * @brief  Decode symbol of Huffman code.
 */
//==============================================================================
static int inf_decode(inflater_t *z, const huff_t *h)
{
        int code = 0, first = 0, index = 0;

        for (int len = 1; len < 16; len++) {
                code |= (int)inf_bits(z, 1);
                int count = h->count[len];
                if (code - count < first) {
                        return h->symbol[index + (code - first)];
                }
                index += count;
                first = (first + count) << 1;
                code <<= 1;
        }

        inf_error(z, "invalid Huffman code");
        return -1;
}

//==============================================================================
/**
 * @brief  Compressed block (fixed or dynamic codes).
 */
//==============================================================================
static void inf_codes(inflater_t *z, memory_t *m, const huff_t *lcode, const huff_t *dcode)
{
        static const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67,
                83, 99, 115, 131, 163, 195, 227, 258};
        static const uint8_t LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5,
                5, 5, 0};
        static const uint16_t DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
                769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint8_t DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11,
                11, 12, 12, 13, 13};

        for (;;) {
                int sym = inf_decode(z, lcode);
                if (sym < 256) {
                        inf_put(z, m, (uint8_t)sym);
                } else if (sym == 256) {
                        return;
                } else {
                        sym -= 257;
                        if (sym >= 29) {
                                inf_error(z, "invalid length code");
                        }

                        uint32_t len = LBASE[sym] + inf_bits(z, LEXT[sym]);
                        int dsym = inf_decode(z, dcode);
                        if (dsym >= 30) {
                                inf_error(z, "invalid distance code");
                        }

                        uint32_t dist = DBASE[dsym] + inf_bits(z, DEXT[dsym]);
                        if (dist > z->st.out) {
                                inf_error(z, "distance too far back");
                        }

                        /* back reference: data written to memory (no window) */
                        for (uint32_t i = 0; i < len; i++) {
                                int64_t a = z->dst + (int64_t)z->st.out - dist;
                                inf_put(z, m, (uint8_t)mem_byte(m, a));
                        }
                }
        }
}

//==============================================================================
/**
 * @brief  One deflate block.
 *
 * @return true: last block.
 */
//==============================================================================
static bool inf_block(inflater_t *z, memory_t *m)
{
        static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        huff_t lcode, dcode;

        bool last = inf_bits(z, 1);
        uint32_t type = inf_bits(z, 2);

        if (type == 0) { /* stored */
                z->st.bitbuf = 0;
                z->st.bitcnt = 0;
                uint32_t len = inf_byte(z);
                len |= inf_byte(z) << 8;
                uint32_t nlen = inf_byte(z);
                nlen |= inf_byte(z) << 8;
                if ((len ^ 0xFFFF) != nlen) {
                        inf_error(z, "invalid stored block");
                }

                for (uint32_t i = 0; i < len; i++) {
                        inf_put(z, m, (uint8_t)inf_byte(z));
                }

        } else if (type == 1) { /* fixed codes */
                uint8_t lengths[288];
                for (int sym = 0; sym < 288; sym++) {
                        lengths[sym] = (sym < 144) ? 8 : (sym < 256) ? 9 : (sym < 280) ? 7 : 8;
                }
                inf_huffman(&lcode, lengths, 288);

                for (int sym = 0; sym < 30; sym++) {
                        lengths[sym] = 5;
                }
                inf_huffman(&dcode, lengths, 30);

                inf_codes(z, m, &lcode, &dcode);

        } else if (type == 2) { /* dynamic codes */
                uint8_t lengths[320];
                int nlen = (int)inf_bits(z, 5) + 257;
                int ndist = (int)inf_bits(z, 5) + 1;
                int ncode = (int)inf_bits(z, 4) + 4;
                if ((nlen > 286) || (ndist > 30)) {
                        inf_error(z, "invalid code counts");
                }

                memset(lengths, 0, 19);
                for (int i = 0; i < ncode; i++) {
                        lengths[ORDER[i]] = (uint8_t)inf_bits(z, 3);
                }

                if (!inf_huffman(&lcode, lengths, 19)) {
                        inf_error(z, "invalid code lengths");
                }

                int idx = 0;
                while (idx < nlen + ndist) {
                        int sym = inf_decode(z, &lcode);
                        if (sym < 16) {
                                lengths[idx++] = (uint8_t)sym;
                        } else {
                                int len = 0, rep;
                                if (sym == 16) {
                                        if (idx == 0) {
                                                inf_error(z, "repeat without length");
                                        }
                                        len = lengths[idx - 1];
                                        rep = 3 + (int)inf_bits(z, 2);
                                } else if (sym == 17) {
                                        rep = 3 + (int)inf_bits(z, 3);
                                } else {
                                        rep = 11 + (int)inf_bits(z, 7);
                                }

                                if (idx + rep > nlen + ndist) {
                                        inf_error(z, "too many lengths");
                                }

                                while (rep--) {
                                        lengths[idx++] = (uint8_t)len;
                                }
                        }
                }

                if (!inf_huffman(&lcode, lengths, nlen) || !inf_huffman(&dcode, lengths + nlen, ndist)) {
                        inf_error(z, "invalid literal/distance code");
                }

                inf_codes(z, m, &lcode, &dcode);

        } else {
                inf_error(z, "invalid block type");
        }

        return last;
}

//==============================================================================
/**
 * @brief  Adler-32 of output in memory.
 */
//==============================================================================
static uint32_t inf_adler32(inflater_t *z, memory_t *m)
{
        uint32_t a = 1, b = 0;

        for (size_t i = 0; i < z->st.out; i++) {
                a = (a + mem_byte(m, z->dst + (int64_t)i)) % 65521;
                b = (b + a) % 65521;
        }

        return (b << 16) | a;
}

//==============================================================================
/**
 * @brief  Decode input from the checkpoint: complete blocks move the
 *         checkpoint, end of input in the middle of a block returns to it.
 */
//==============================================================================
static void inf_run(inflater_t *z, memory_t *m)
{
        z->st = z->cp;

        int r = setjmp(z->jmp);
        if (r == 1) { /* end of input: next feed continues from the checkpoint */
                z->st = z->cp;
                return;
        } else if (r == 2) {
                return; /* error */
        }

        if (z->st.phase == INF_HEADER) {
                uint32_t cmf = inf_byte(z);
                uint32_t flg = inf_byte(z);
                if (((cmf & 0x0F) != 8) || (((cmf << 8) | flg) % 31 != 0) || (flg & 0x20)) {
                        inf_error(z, "invalid zlib header");
                }
                z->st.phase = INF_BLOCKS;
                z->cp = z->st;
        }

        while (z->st.phase == INF_BLOCKS) {
                bool last = inf_block(z, m);
                if (last) {
                        z->st.bitbuf = 0; /* trailer is byte aligned */
                        z->st.bitcnt = 0;
                        z->st.phase = INF_TRAILER;
                }
                z->cp = z->st;
        }

        if (z->st.phase == INF_TRAILER) {
                uint32_t adler = 0;
                for (int i = 0; i < 4; i++) {
                        adler = (adler << 8) | inf_byte(z);
                }
                z->adler_ok = (adler == inf_adler32(z, m));
                z->st.phase = INF_DONE;
                z->cp = z->st;
        }
}

//==============================================================================
/**
 * @brief  Lua: decoder:feed(mem, data) -> state, used, out [, adler_ok]
 *
 *         Decodes data appended to the input of the stream, output is written
 *         to memory. state: "more" (all data belong to the stream, more data
 *         are needed), "done" (used: number of bytes of data that belong to
 *         the stream, rest belongs to the following data; adler_ok: checksum
 *         is correct), "error" (used: message). out: number of output bytes.
 */
//==============================================================================
static int l_inflater_feed(lua_State *L)
{
        inflater_t *z = check_inflater(L, 1);
        memory_t *m = check_memory(L, 2);
        size_t len;
        const char *data = luaL_checklstring(L, 3, &len);

        if ((z->cp.phase == INF_DONE) || (z->cp.phase == INF_ERROR)) {
                return luaL_error(L, "inflater: stream is finished");
        }

        /* input buffer: bytes from the checkpoint + new data */
        size_t keep = z->cp.in - z->in_base;
        if (keep > 0) {
                memmove(z->in, z->in + keep, z->in_len - keep);
                z->in_len -= keep;
                z->in_base += keep;
        }

        if (z->in_len + len > z->in_cap) {
                size_t cap = (z->in_len + len) * 2;
                uint8_t *buf = realloc(z->in, cap);
                if (!buf) {
                        return luaL_error(L, "inflater: not enough memory");
                }
                z->in = buf;
                z->in_cap = cap;
        }

        size_t start = z->in_base + z->in_len; /* stream position of data */
        memcpy(z->in + z->in_len, data, len);
        z->in_len += len;

        inf_run(z, m);

        if (z->st.phase == INF_ERROR) {
                z->cp.phase = INF_ERROR;
                lua_pushliteral(L, "error");
                lua_pushstring(L, z->err);
                lua_pushinteger(L, (lua_Integer)z->st.out);
                return 3;
        }

        if (z->cp.phase == INF_DONE) {
                lua_pushliteral(L, "done");
                lua_pushinteger(L, (lua_Integer)(z->cp.in - start));
                lua_pushinteger(L, (lua_Integer)z->cp.out);
                lua_pushboolean(L, z->adler_ok);
                return 4;
        }

        lua_pushliteral(L, "more");
        lua_pushinteger(L, (lua_Integer)len);
        lua_pushinteger(L, (lua_Integer)z->cp.out);
        return 3;
}

//==============================================================================
/**
 * @brief  Lua: decoder is collected.
 */
//==============================================================================
static int l_inflater_gc(lua_State *L)
{
        inflater_t *z = check_inflater(L, 1);
        free(z->in);
        z->in = NULL;
        z->in_len = z->in_cap = 0;
        return 0;
}

//==============================================================================
/**
 * @brief  Push table of graphics functions (global "gfx" of board scripts).
 */
//==============================================================================
void board_gfx_open(lua_State *L)
{
        static const luaL_Reg MEMORY_METHODS[] = {
                {"get", l_memory_get},
                {"set", l_memory_set},
                {"read", l_memory_read},
                {"write", l_memory_write},
                {"fill", l_memory_fill},
                {"copy", l_memory_copy},
                {"crc32", l_memory_crc32},
                {NULL, NULL},
        };

        static const luaL_Reg CANVAS_METHODS[] = {
                {"scissor", l_canvas_scissor},
                {"tag", l_canvas_tag},
                {"clear", l_canvas_clear},
                {"rect", l_canvas_rect},
                {"disc", l_canvas_disc},
                {"line", l_canvas_line},
                {"rrect", l_canvas_rrect},
                {"bitmap", l_canvas_bitmap},
                {"tag_at", l_canvas_tag_at},
                {"pixel", l_canvas_pixel},
                {"reset", l_canvas_reset},
                {"push", l_canvas_push},
                {NULL, NULL},
        };

        static const luaL_Reg FUNCTIONS[] = {
                {"memory", l_memory_new},
                {"canvas", l_canvas_new},
                {"inflater", l_inflater_new},
                {NULL, NULL},
        };

        luaL_newmetatable(L, MEMORY_METATABLE);
        luaL_newlib(L, MEMORY_METHODS);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, l_memory_len);
        lua_setfield(L, -2, "__len");
        lua_pushcfunction(L, l_memory_gc);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);

        luaL_newmetatable(L, CANVAS_METATABLE);
        luaL_newlib(L, CANVAS_METHODS);
        lua_pushcclosure(L, l_canvas_index, 1);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, l_canvas_gc);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);

        static const luaL_Reg INFLATER_METHODS[] = {
                {"feed", l_inflater_feed},
                {NULL, NULL},
        };

        luaL_newmetatable(L, INFLATER_METATABLE);
        luaL_newlib(L, INFLATER_METHODS);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, l_inflater_gc);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);

        luaL_newlib(L, FUNCTIONS);
}

/*==============================================================================
  End of file
==============================================================================*/
