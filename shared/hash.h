/*
    hash.h - hash function declarations

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

#include "types.h"

/**
 * Hashes a string.
 */
u32 rrc_hash_string(const char *str);

/**
 * Hashes a string while treating the characters as all lowercase. This helps normalization (when you want to hash case-insensitively)
 * and is a more efficient alternative to lowercasing the string separately.
 */
u32 rrc_hash_string_lowercase(const char *str);
