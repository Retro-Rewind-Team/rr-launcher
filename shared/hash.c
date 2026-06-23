/*
    hash.c - hash function implementations

    Copyright (C) 2026  Retro Rewind Team

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

#include "hash.h"
#include <ctype.h>

/**
 * These hash functions implement a simple multiplicative string hash used by Java. It's both simple and fast and provides near collision-free hashes
 * for our strings (note that, we know our strings for the most part ahead of time, so the hashes don't need to be high quality in the general case,
 * as long as it's tested on our specific set of strings).
 *
 * The multiplier 31 was chosen because it's an odd prime, which helps with hash distribution: https://stackoverflow.com/a/299748
 */
#define _RTE_HASH_STRING_MULTIPLIER 31

u32 rrc_hash_string(const char *str)
{
    const unsigned char *ustr = (const unsigned char *)str;
    u32 hash = 0;
    while (*ustr)
    {
        hash = hash * _RTE_HASH_STRING_MULTIPLIER + *ustr;
        ustr++;
    }
    return hash;
}

u32 rrc_hash_string_lowercase(const char *str)
{
    const unsigned char *ustr = (const unsigned char *)str;
    u32 hash = 0;
    while (*ustr)
    {
        hash = hash * _RTE_HASH_STRING_MULTIPLIER + (unsigned char)tolower(*ustr);
        ustr++;
    }
    return hash;
}
