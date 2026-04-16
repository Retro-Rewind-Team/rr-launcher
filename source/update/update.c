/*
    update.c - distribution update implementation
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

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <gctypes.h>
#include <zip.h>
#include <errno.h>
#include <wiisocket.h>

#include "versionsfile.h"
#include "update.h"
#include "../sd.h"
#include "../util.h"
#include "../console.h"
#include "../time.h"
#include "../prompt.h"
#include "../shutdown.h"
#include "../version.h"

#define _RRC_UPDATE_ZIP_NAME "update.zip"

struct rrc_result rrc_update_get_current_version(struct rrc_version *version)
{
    FILE *file = fopen(RRC_VERSIONFILE, "r");
    if (file == NULL)
    {
        return rrc_result_create_error_errno(errno, "Failed to open version file " RRC_VERSIONFILE " for reading");
    }

    int fd = fileno(file);
    struct stat statbuf;
    int res = stat(RRC_VERSIONFILE, &statbuf);
    if (res != 0)
    {
        return rrc_result_create_error_errno(errno, "Failed to stat version file to get its file size");
    }

    int sz = statbuf.st_size;
    char verstring[sz + 1];
    if (read(fd, (void *)verstring, sz) == 0)
    {
        return rrc_result_create_error_errno(errno, "Failed to read version string from file");
    }

    if (fclose(file) != 0)
    {
        return rrc_result_create_error_errno(errno, "Failed to close version file");
    }

    /* add null termination */
    verstring[sz] = '\0';

    return rrc_version_from_string(verstring, version);
}

struct rrc_result rrc_update_set_current_version(struct rrc_version *version)
{
    int p1 = version->major;
    int p2 = version->minor;
    int p3 = version->patch;

    char out[32];
    int written = snprintf(out, sizeof(out), "%d.%d.%d", p1, p2, p3);
    RRC_ASSERT(written < sizeof(out), "version string too long");
    FILE *file = fopen(RRC_VERSIONFILE, "w");
    if (file == NULL)
    {
        return rrc_result_create_error_errno(errno, "Failed to open version file for writing");
    }

    if (fwrite(out, 1, written, file) != written)
    {
        return rrc_result_create_error_errno(errno, "Failed to write version string");
    }

    if (fclose(file) == 0)
    {
        return rrc_result_success;
    }
    else
    {
        return rrc_result_create_error_errno(errno, "Failed to close version file");
    }
}

int lp = -1;
rrc_time_tick last_measurement_from = -1;
curl_off_t last_dlnow = 0;
int last_second_dl_amount = 0;

int _rrc_zipdl_progress_callback(int *numinfo,
                                 curl_off_t dltotal,
                                 curl_off_t dlnow,
                                 curl_off_t ultotal,
                                 curl_off_t ulnow)
{
    /* 100kB chunks */
#define _RRC_PROGRESS_UPD_CHUNKSIZE 100000

    /* update download speed every second */
#define _RRC_PROGRESS_UPD_SPEED_INC 1000
    int progress = (dlnow * 100) / dltotal;

    if (diff_msec(last_measurement_from, gettime()) > _RRC_PROGRESS_UPD_SPEED_INC || last_measurement_from < 0)
    {
        last_measurement_from = gettime();

        last_second_dl_amount = dlnow - last_dlnow;
        last_dlnow = dlnow;
    }

    int chunk = dlnow / _RRC_PROGRESS_UPD_CHUNKSIZE;
    if (chunk != lp)
    {
        lp = chunk;
        char msg[100];
        snprintf(
            msg,
            100,
            "Downloading update %i of %i - %i kB/s (%i/%i kB)",
            ((*numinfo) / 100) + 1,
            (*numinfo) % 100,
            (int)(last_second_dl_amount / 1000),
            (int)(dlnow / (curl_off_t)1000),
            (int)(dltotal / (curl_off_t)1000));

        rrc_con_update(msg, progress);
    }
    return 0;
#undef _RRC_PROGRESS_UPD_CHUNKSIZE
}

size_t _rrc_zipdl_write_data_callback(char *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

size_t _rrc_update_writefunction_empty(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    return size * nmemb;
}

/*
    Get the content-length of a ZIP download in bytes.
*/
CURLcode _rrc_update_get_zip_size(char *url, curl_off_t *size)
{
    CURLcode cres;
    CURL *curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _rrc_update_writefunction_empty);
        cres = curl_easy_perform(curl);
        if (cres != CURLE_OK)
        {
            return cres;
        }
        cres = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, size);
        curl_easy_cleanup(curl);
        return cres;
    }

    return CURLE_FAILED_INIT;
}

struct rrc_result rrc_update_download_zip(char *url, char *filename, int current_zip, int max_zips)
{
    CURLcode cres;
    CURL *curl = curl_easy_init();
    FILE *fp;
    int numinfo = (current_zip * 100) + max_zips;
    if (curl)
    {
        fp = fopen(filename, "wb");
        if (fp == NULL)
        {
            return rrc_result_create_error_errno(errno, "Failed to create temporary ZIP file for update download");
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &numinfo);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, _rrc_zipdl_progress_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _rrc_zipdl_write_data_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

        /* Perform the request, cres gets the return code */
        cres = curl_easy_perform(curl);

        /* Check for errors */
        if (cres != CURLE_OK)
        {
            // TODO: report error better
            printf("curl_easy_perform() failed: %s\n",
                   curl_easy_strerror(cres));

            fclose(fp);
            curl_easy_cleanup(curl);
            return rrc_result_create_error_curl(cres, "Failed to download update ZIP");
        }

        fclose(fp);
        curl_easy_cleanup(curl);
        return rrc_result_success;
    }

    return rrc_result_create_error_curl(CURLE_FAILED_INIT, "Failed to init curl");
}

#define RETURN_IO_ERR(err)            \
    do                                \
    {                                 \
        res->ecode = err;             \
        res->inner.errnocode = errno; \
        return;                       \
    } while (0);

/**
 * Creates any directories for a given path like "a/b/c/d.txt", one at a time, starting with the outermost dir.
 */
static struct rrc_result mkdir_recursive(const char *fp)
{
    char tmp_path[PATH_MAX];
    RRC_ASSERT(strlen(fp) < PATH_MAX, "path should never be longer than PATH_MAX");

    // start at 1 to skip any leading slash
    for (int i = 1; i < strlen(fp); i++)
    {
        if (fp[i] == '/')
        {
            strncpy(tmp_path, fp, i);
            tmp_path[i] = '\0';
            int err = mkdir(tmp_path, 0777);
            if (err != 0 && errno != EEXIST)
            {
                return rrc_result_create_error_errno(errno, "Failed to create recursive directories for path");
            }
        }
    }

    return rrc_result_success;
}

struct rrc_result rrc_update_extract_zip_archive()
{
    int zip_err;
    struct zip *archive = zip_open(_RRC_UPDATE_ZIP_NAME, ZIP_CHECKCONS | ZIP_RDONLY, &zip_err);
    if (archive == NULL)
    {
        return rrc_result_create_error_zip(zip_err, "Failed to open downloaded ZIP archive");
    }

    u32 zip_entries = zip_get_num_entries(archive, 0);

    char buf[4096];

    for (int i = 0; i < zip_entries; i++)
    {
        zip_stat_t stat;
        int err = zip_stat_index(archive, i, 0, &stat);
        if (err != 0)
        {
            return rrc_result_create_error_zip(err, "Failed to stat file in archive");
        }

        if (!(stat.valid & (ZIP_STAT_SIZE | ZIP_STAT_NAME | ZIP_STAT_SIZE)))
        {
            return rrc_result_create_error_misc_update("ZIP Archive entry contains invalid attributes");
        }

        if (stat.name[0] == 0)
        {
            return rrc_result_create_error_misc_update("Empty file name in ZIP archive");
        }

        unsigned long sd_free;
        TRY(rrc_sd_get_free_space(&sd_free));

        if (stat.size > sd_free)
        {
            return rrc_result_create_error_misc_update("Not enough free space on SD card for update");
        }

        if (stat.name[strlen(stat.name) - 1] == '/')
        {
            // Ignore directories. They are automatically created when processing files.
            continue;
        }

        zip_file_t *zip_file = zip_fopen_index(archive, i, ZIP_FL_ENC_UTF_8);
        if (!zip_file)
        {
            return rrc_result_create_error_misc_update("Failed to open file in ZIP archive");
        }

        const char *filepath = stat.name;

        char message[128];
        snprintf(message, sizeof(message), "Extracting %s (%d/%d)", filepath, i + 1, zip_entries);
        rrc_con_update(message, ((f64)(i + 1) / (f64)zip_entries) * 100);

        FILE *outfile = fopen(filepath, "w");
        if (!outfile)
        {
            // We couldn't create the file. This can happen if we have a file path like "a/b.txt" and directory "a" doesn't exist.
            // At least this ENOENT case is recoverable by recursively creating the missing directories, so only return error for all other errors.
            if (errno != ENOENT)
            {
                return rrc_result_create_error_errno(errno, "Failed to create output file for extracting ZIP entry");
            }

            TRY(mkdir_recursive(filepath));

            outfile = fopen(filepath, "w");
            if (!outfile)
            {
                // We're still getting errors when opening the file even after creating missing directories. Nothing more we can do.
                return rrc_result_create_error_errno(errno, "Failed to open output file for extracting ZIP entry after creating directories");
            }
        }

        int read;
        while ((read = zip_fread(zip_file, buf, sizeof(buf))) > 0)
        {
            int written = fwrite(buf, 1, read, outfile);
            if (written != read)
            {
                return rrc_result_create_error_errno(errno, "Failed to fully write ZIP chunk");
            }
        }

        if (read < 0)
        {
            return rrc_result_create_error_errno(errno, "Failed to write ZIP chunk");
        }

        fclose(outfile);
        zip_fclose(zip_file);
    }

    zip_close(archive);
    return rrc_result_success;
}

int rrc_update_get_total_update_size(struct rrc_update_state *state, curl_off_t *size)
{
    *size = 0;

    for (int i = 0; i < state->num_updates; i++)
    {
        int ret;
        curl_off_t this_size;

        ret = _rrc_update_get_zip_size(state->update_urls[i], &this_size);
        if (ret != 0)
        {
            *size = 0;
            return -ret;
        }

        *size += this_size;
    }

    return 0;
}

int rrc_update_is_large(struct rrc_update_state *state, curl_off_t *size)
{
    int ret = rrc_update_get_total_update_size(state, size);

    if (ret < 0)
    {
        return ret;
    }

    return (*size > RRC_UPDATE_LARGE_THRESHOLD);
}

struct rrc_result rrc_update_do_updates_with_state(struct rrc_update_state *state)
{
    while (state->current_update_num < state->num_updates)
    {
        /* We can check for this between updates since we have no in-flight information */
        rrc_shutdown_check();

        char *url = state->update_urls[state->current_update_num];

        curl_off_t zipsz;
        CURLcode szres = _rrc_update_get_zip_size(url, &zipsz);
        if (szres != CURLE_OK)
        {
            return rrc_result_create_error_curl(szres, "Failed to get update ZIP size");
        }

        unsigned long sd_free;
        TRY(rrc_sd_get_free_space(&sd_free));

        if (zipsz > sd_free)
        {
            return rrc_result_create_error_misc_update("Not enough free space on SD card for update");
        }

        TRY(rrc_update_download_zip(url, _RRC_UPDATE_ZIP_NAME, state->current_update_num, state->num_updates));

        struct stat sb;
        int s = stat(_RRC_UPDATE_ZIP_NAME, &sb);
        if (s == -1)
        {
            return rrc_result_create_error_errno(errno, "Failed to stat update ZIP file");
        }

        TRY(rrc_update_extract_zip_archive());

        int rres = remove(_RRC_UPDATE_ZIP_NAME);
        if (rres == -1)
        {
            return rrc_result_create_error_errno(errno, "Failed to remove temporary update file");
        }

        // Now remove any deleted files.
        for (int i = 0; i < state->num_deleted_files; i++)
        {
            struct rrc_versionsfile_deleted_file *file = &state->deleted_files[i];
            if (rrc_version_equals(&file->version, &state->update_versions[state->current_update_num]))
            {
                char out[100];
                snprintf(out, 100, "Removing deleted file %s\n", file->path);
                rrc_con_update(out, 100);
                int rmres = remove(file->path);
                if (rmres != 0 && errno != ENOENT)
                {
                    return rrc_result_create_error_errno(errno, "Failed to remove deleted file for update");
                }
            }
        }

        // Update the version.txt file
        TRY(rrc_update_set_current_version(&state->update_versions[state->current_update_num]));

        state->current_update_num++;
    }

    return rrc_result_success;
}

struct rrc_result rrc_update_do_updates(void *xfb, int *count, bool *updates_installed)
{
    rrc_con_clear(true);

    rrc_con_update("Prepare Network", 0);
    int res = wiisocket_init();
    if (res < 0)
    {
        return rrc_result_create_error_misc_update("Failed to connect to the internet. Please check your connection and internet settings.");
    }

    *updates_installed = false;
    char *versionsfile = NULL;
    char *deleted_versionsfile = NULL;
    int num_deleted_files = 0;
    struct rrc_versionsfile_deleted_file *deleted_files = NULL;
    char **zip_urls = NULL;
    // Packed array of version (non-pointer)
    struct rrc_version *update_versions = NULL;
    rrc_con_update("Get Versions", 10);
    res = rrc_versionsfile_get_versionsfile(&versionsfile);
    if (res < 0)
    {
        return rrc_result_create_error_curl(-res, "Failed to get version information.");
    }

    struct rrc_version current = {-1, -1, -1};
    TRY(rrc_update_get_current_version(&current));

    RRC_ASSERT(current.major >= 0, "failed to read current version file");
    rrc_dbg_printf("Current version: %i.%i.%i\n", current.major, current.minor, current.patch);

    rrc_con_update("Get Download URLs", 20);
    TRY(rrc_versionsfile_get_necessary_urls_and_versions(versionsfile, &current, count, &zip_urls, &update_versions));

    if (*count > 0)
    {
        char *lines[] = {"An update is available."};

        enum rrc_prompt_result result = rrc_prompt_2_options(xfb, lines, 1, "Update", "Skip", RRC_PROMPT_RESULT_YES, RRC_PROMPT_RESULT_NO);
        if (result == RRC_PROMPT_RESULT_NO)
        {
            return rrc_result_success;
        }
    }

    rrc_con_update("Get Files to Remove", 30);
    res = rrc_versionsfile_get_removed_files(&deleted_versionsfile);
    if (res < 0)
    {
        RRC_FATAL("couldnt get files to remove! res: %i\n", res);
    }

    TRY(rrc_versionsfile_parse_deleted_files(deleted_versionsfile, &current, &deleted_files, &num_deleted_files));

    rrc_dbg_printf("%i updates\n", *count);
    struct rrc_update_state state =
        {
            .current_update_num = 0,
            .d_ptr = NULL,
            .num_updates = *count,
            .update_urls = zip_urls,
            .update_versions = update_versions,
            .current_version = current,
            .num_deleted_files = num_deleted_files,
            .deleted_files = deleted_files};

    rrc_con_update("Check Update Size", 40);
    curl_off_t updates_size;
    int is_large = rrc_update_is_large(&state, &updates_size);
    RRC_ASSERT(is_large >= 0, "failed to get update size");

    if (is_large == 1)
    {
        char info_line1[128];
        char info_line2[128];
        snprintf(info_line1, 128, "There are %i updates available,", state.num_updates);
        snprintf(info_line2, 128, "totalling %iMB of data to download.", (int)(updates_size / 1000 / 1000));
        char *lines[] = {
            info_line1,
            info_line2,
            "This may take a long time!",
            "It may be quicker to reinstall the pack from your computer.",
            "",
            "Would you like to continue?"};

        enum rrc_prompt_result result = rrc_prompt_yes_no(xfb, lines, 6);
        RRC_ASSERT(result != RRC_PROMPT_RESULT_ERROR, "failed to generate prompt");
        if (result == RRC_PROMPT_RESULT_NO)
        {
            return rrc_result_success;
        }
    }

    TRY(rrc_update_do_updates_with_state(&state));

    *updates_installed = true;
    return rrc_result_success;
}
