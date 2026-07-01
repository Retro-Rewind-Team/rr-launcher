/*
    versionsfile.h - fetching and parsing of version info header
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

#ifndef RRC_VERSIONSFILE_H
#define RRC_VERSIONSFILE_H

#include "../result.h"
#include "../versionutils.h"

struct rrc_versionsfile_deleted_file
{
    struct rrc_version version;
    char *path;
};

/*
    in: input string
    by: char to split by
    out: pointer to array of output
    count: amount of elements

    for convenience this will ignore trailing characters of `by'
    this is limited to 4096 entries, if it exceeds this the return code will be 2

    **REMEMBER TO FREE EVERYTHING WHEN DONE - USE `_rrc_versionfile_free_split'**
*/
int rrc_versionsfile_split_by(char *in, char by, char ***out, int *amt);
void rrc_versionsfile_free_split(char **array, int count);

/*
    Get version information from Retro Rewind servers.
    On success, return code is 0 and `result' is populated with a NULL-terminated string.
    On failure, return code is negative CURL return code and `result' is NULL.
*/
int rrc_versionsfile_get_versionsfile(char **result);

/*
    Get files that were removed from each version.
    On success, return code is 0.
    On failure, return code is negative CURL return code and `result' is NULL.
*/
int rrc_versionsfile_get_removed_files(char **result);

/*
    Get an array of all URLs we need to download, where the first index needs downloading first.
    On success, return code is 0 and `result' is populated with an array of strings and `count' is set to the amount of entries.
*/
struct rrc_result rrc_versionsfile_get_necessary_urls_and_versions(char *versionsfile, struct rrc_version *current_version, int *uamt, char ***urls, struct rrc_version **versions);

struct rrc_result rrc_versionsfile_parse_deleted_files(char *input, struct rrc_version *current_version, struct rrc_versionsfile_deleted_file **output, int *amt);

#endif
