/*
    util.h - utility constants and macro definitions

    Copyright (C) 2025  Retro Rewind Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef RRC_UTIL_H
#define RRC_UTIL_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <gctypes.h>

#include "console.h"

/*
    When this directive is defined and set to a value greater than 0,
    debug logging and some additional assertions are enabled.
*/
#define DEBUG 1

#define _RRC_STRING(s) #s

// Whenever we have a loop waiting for wiipad button inputs, this timeout is used (in microseconds).
#define RRC_WPAD_LOOP_TIMEOUT 20000

#ifndef RRC_EXIT_DELAY
#define RRC_EXIT_DELAY 3000000
#endif

#define RRC_LWP_PRIO_IDLE 0
#define RRC_LWP_OK 0

#define RRC_FATAL(...)          \
    do                          \
    {                           \
        printf(__VA_ARGS__);    \
        usleep(RRC_EXIT_DELAY); \
        exit(1);                \
    } while (0);

#define RRC_ASSERT(condition, what)                                        \
    do                                                                     \
    {                                                                      \
        int st = condition;                                                \
        char *st_str = _RRC_STRING(condition);                             \
        if (!st)                                                           \
        {                                                                  \
            RRC_FATAL("%s: assert failed: %s (got %i)", what, st_str, st); \
        }                                                                  \
    } while (0);

#define RRC_ASSERTEQ(lhs, rhs, what)                                                         \
    do                                                                                       \
    {                                                                                        \
        int st = lhs == rhs;                                                                 \
        char *st_str = _RRC_STRING(lhs) " == " _RRC_STRING(rhs);                             \
        if (!st)                                                                             \
        {                                                                                    \
            RRC_FATAL("%s: assert failed: %s (lhs = %i, rhs = %i)", what, st_str, lhs, rhs); \
        }                                                                                    \
    } while (0);

#define RRC_STRINGIFY(x) #x

#if defined(DEBUG) && DEBUG >= 0
/* define debug macros */

#define rrc_dbg_printf(...)                                        \
    do                                                             \
    {                                                              \
        rrc_con_clear_line(_RRC_PRINTF_ROW);                       \
        rrc_con_clear_line(_RRC_PRINTF_ROW + 1);                   \
        rrc_con_cursor_seek_to(_RRC_PRINTF_ROW, RRC_CON_EDGE_PAD); \
        printf(__VA_ARGS__);                                       \
    } while (0);

#else
/* define stubs */

#define rrc_dbg_printf(...)

#endif

/**
 * Invalidates any data caches for a given address range.
 * This handles the pointer alignment and size-multiple requirements and does not need to be handled by callers.
 */
void rrc_invalidate_cache(void *addr, u32 size);

u32 align_down(u32 num, u32 align_as);
u32 align_up(u32 num, u32 align_as);

#endif
