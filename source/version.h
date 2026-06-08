/*
    version.h - version structure and related functions

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

#ifndef RRC_VERSION_H
#define RRC_VERSION_H

#include <gctypes.h>
#include "result.h"

#define _RRC_INTERNAL_VERSION_MAJOR 0
#define _RRC_INTERNAL_VERSION_MINOR 9
#define _RRC_INTERNAL_VERSION_PATCH 0

#define RRC_INTERNAL_VERSION ((struct rrc_version){.major = _RRC_INTERNAL_VERSION_MAJOR, \
                                                   .minor = _RRC_INTERNAL_VERSION_MINOR, \
                                                   .patch = _RRC_INTERNAL_VERSION_PATCH})

struct rrc_version
{
    int major;
    int minor;
    int patch;
};

/// Returns true if version a is older than version b, false otherwise
bool rrc_version_is_older(const struct rrc_version *a, const struct rrc_version *b);

/// Parses a version string of the format "major.minor.patch" into a rrc_version struct. Returns an error if the format is invalid.
struct rrc_result rrc_version_from_string(const char *version_str, struct rrc_version *out_version);

bool rrc_version_equals(const struct rrc_version *a, const struct rrc_version *b);

#endif /* RRC_VERSION_H */
