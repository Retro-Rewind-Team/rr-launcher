/*
    versionutils.c - utility function implementations for working with `rrc_version`s

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

#include "versionutils.h"
#include <string.h>
#include <stdlib.h>

bool rrc_version_is_older(const struct rrc_version *a, const struct rrc_version *b)
{
    if (a->major < b->major)
        return true;
    else if (a->major > b->major)
        return false;

    if (a->minor < b->minor)
        return true;
    else if (a->minor > b->minor)
        return false;

    if (a->patch < b->patch)
        return true;
    else
        return false;
}

struct rrc_result rrc_version_from_string(const char *version_str, struct rrc_version *out_version)
{
    // FIXME: Support 4-part version strings later?

    /* major, minor, revision */
    int parts[3] = {0, 0, 0};
    int current = 0, started_at = 0;
    int len = strlen(version_str);
    for (int i = 0; i < len + 1; i++)
    {
        /* read until . or EOF, then extract that section */
        if (version_str[i] == '.' || version_str[i] == '\0')
        {
            int sect_len = i - started_at;
            if (sect_len == 0)
            {
                /* ??? */
                return rrc_result_create_error_corrupted_versionfile("Invalid format");
            }

            /* read this section */
            char section[sect_len + 1];
            int idx = 0;
            for (int j = started_at; j < started_at + sect_len; j++)
            {
                section[idx] = version_str[j];
                idx++;
            }
            section[sect_len] = '\0';

            int section_l = strtol(section, NULL, 10);
            parts[current] = section_l;
            current++;
            if (current > 2)
            {
                /* we read the rev now */
                break;
            }

            started_at = i + 1;
        }
    }

    out_version->major = parts[0];
    out_version->minor = parts[1];
    out_version->patch = parts[2];

    return rrc_result_success;
}

bool rrc_version_equals(const struct rrc_version *a, const struct rrc_version *b)
{
    return (a->major == b->major) && (a->minor == b->minor) && (a->patch == b->patch);
}
