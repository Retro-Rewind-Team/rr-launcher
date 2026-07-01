/*
    update.h - distribution update headers
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

#ifndef RRC_UPDATE_H
#define RRC_UPDATE_H

#include <dir.h>
#include <curl/curl.h>
#include "../result.h"
#include "../versionutils.h"

#define RRC_UPDATE_LARGE_THRESHOLD (long)(1000 * 1000 * 100) /* 100MB */
#define RRC_VERSIONFILE "/" RRC_RETRO_REWIND_BASE_DIR "/version.txt"

/* Holds all info related to an update or sequence of updates */
struct rrc_update_state
{
    /* Amount of updates needed */
    int num_updates;
    /* The current update */
    int current_update_num;
    /* Pointer to current downloaded data. Resets at the start of each download */
    char *d_ptr;
    /* All URLs for updates, in order. Length = num_updates */
    char **update_urls;
    /* Version of each update. Has the same length as `update_urls` and each index into update_urls is also valid for update_versions */
    struct rrc_version *update_versions;
    /* The current version. */
    struct rrc_version current_version;
    /* Amount of files to delete. */
    int num_deleted_files;
    /* Files to delete. */
    struct rrc_versionsfile_deleted_file *deleted_files;
};

/*
    Returns an int specifying version information from version.txt.
    E.g., 4.2.0 = 420
    SD driver must be loaded for this to work.
*/
struct rrc_result rrc_update_get_current_version(struct rrc_version *version);

/*
    Writes the specified version int into version.txt.
    SD driver must be loaded for this to work.
*/
struct rrc_result rrc_update_set_current_version(struct rrc_version *version);

/*
    Downloads a Retro Rewind ZIP. Uses the console to display progress.
    Stores on SD in the file given by `filename'.
*/
struct rrc_result rrc_update_download_zip(char *url, char *filename, int current_zip, int max_zips);

/*
    Get the total size of all update ZIPs in bytes. This can be used to determine whether
    to warn the user that updating will take a long time based on some arbitrary threshold.

    On success, returns 0 and `size' is populated with the total download size. On failure,
    the return code is negative (usually a cURL error code), and `size' is zero.
*/
int rrc_update_get_total_update_size(struct rrc_update_state *state, curl_off_t *size);

/*
    Determines if a update or sequence of updates is large. This is determined based on
    if the total update download size is above some arbitrary value defined in
    RRC_UPDATE_LARGE_THRESHOLD, in bytes.

    Returns 0 if the update is not large (or there are none), 1 if it is large, and
    a negative error status on failure.
*/
int rrc_update_is_large(struct rrc_update_state *state, curl_off_t *size);

/*
    Does all updates specified in update_urls, in order.
    This involves sequentially donloading, unzipping, and applying each one
    TODO: maybe make this threaded so if we have multiple updates we can download
    one and apply one at the same time?

    Returns 0 on success and a negative code on fail.
    `res' is a pointer to a valid `struct rrc_update_result' on return.
*/
struct rrc_result rrc_update_do_updates_with_state(struct rrc_update_state *state);

/*
    Checks if updates are needed, and if there are, prompt the user and and download them. See `rrc_update_do_updates_with_state` for more details.
    This also writes the number of available updates into `count' and returns whether the updates were actually installed.
*/
struct rrc_result rrc_update_do_updates(void *xfb, int *count, bool *any_updates_installed);

#endif
