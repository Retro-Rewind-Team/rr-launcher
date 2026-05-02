/*
    util.c - utility function implementations

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

#include <gctypes.h>
#include <sys/statvfs.h>
#include <gccore.h>
#include "result.h"

u32 align_down(u32 num, u32 align_as)
{
    return num & -align_as;
}

u32 align_up(u32 num, u32 align_as)
{
    return (num + align_as - 1) & -align_as;
}

void rrc_invalidate_cache(void *addr, u32 size)
{
    // Must be aligned to a 32 byte boundary.
    addr = (void *)align_down((u32)addr, 32);

    // Size must be a multiple of 32.
    // We add 32 to the size so that in case the address was not aligned and we had to align down,
    // we don't end up skipping the last cache line.
    size = align_up(size + 32, 32);

    DCFlushRange(addr, size);
    ICInvalidateRange(addr, size);
}
