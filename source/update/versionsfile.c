/*
    versionsfile.c - fetching and parsing of version info implementation
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>
#include <string.h>

#include "../console.h"
#include "../util.h"
#include "versionsfile.h"

#define _RRC_VERSIONSFILE_URL "http://update.rwfc.net:8000/RetroRewind/RetroRewindVersion.txt"
#define _RRC_VERSIONS_FILE_REMOVED_URL "http://update.rwfc.net:8000/RetroRewind/RetroRewindDelete.txt"
// max array size
#define _RRC_SPLIT_LIM 4096

struct ptr_len_pair
{
    int len;
    char *ptr;
};

int _rrc_versionsfile_progress_callback(char *update,
                                        curl_off_t dltotal,
                                        curl_off_t dlnow,
                                        curl_off_t ultotal,
                                        curl_off_t ulnow)
{
    static int lp = -1;
    int progress = (dlnow * 100) / dltotal;
    if (progress != lp)
    {
        lp = progress;
        rrc_con_update(update, progress);
    }
    return 0;
}

size_t _rrc_versionsfile_write_callback(char *ptr, size_t size, size_t nmemb, void *ss)
{
    struct ptr_len_pair *s = (struct ptr_len_pair *)ss;
    size_t new_len = s->len + size * nmemb;
    s->ptr = realloc(s->ptr, new_len + 1);
    if (s->ptr == NULL)
    {
        // TODO: maybe make a proper place to put error info?
        printf("realloc failed!");
        return -1;
    }
    memcpy(s->ptr + s->len, ptr, size * nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;

    return size * nmemb;
}

int rrc_versionsfile_get_versionsfile(char **result)
{
    struct ptr_len_pair s;
    s.len = 0;
    s.ptr = malloc(s.len + 1);
    if (s.ptr == NULL)
    {
        printf("malloc failed!");
        return -1;
    }
    s.ptr[0] = '\0';

    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, _RRC_VERSIONSFILE_URL);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, _rrc_versionsfile_progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)"Fetching Version Info");
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _rrc_versionsfile_write_callback);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L); // 10 second connection timeout
        // Set low speed limit to 30 bytes/s for at least 60 seconds before aborting
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 30L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK)
        {
            // TODO: report error better
            return -res;
        }

        /* always cleanup */
        curl_easy_cleanup(curl);
    }

    if (result != NULL)
    {
        *result = s.ptr;
    }

    return 0;
}

int rrc_versionsfile_get_removed_files(char **result)
{
    struct ptr_len_pair s;
    s.len = 0;
    s.ptr = malloc(s.len + 1);
    if (s.ptr == NULL)
    {
        printf("malloc failed!");
        return -1;
    }
    s.ptr[0] = '\0';

    CURL *curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, _RRC_VERSIONS_FILE_REMOVED_URL);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, _rrc_versionsfile_progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)"Fetching Removed Files");
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _rrc_versionsfile_write_callback);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L); // 10 second connection timeout
        // Set low speed limit to 30 bytes/s for at least 60 seconds before aborting
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 30L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
        
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            // TODO: report error better
            printf("curl_easy_perform() failed: %s\n",
                   curl_easy_strerror(res));
            return -res;
        }

        curl_easy_cleanup(curl);
    }

    *result = s.ptr;
    return 0;
}

int rrc_versionsfile_split_by(char *in, char by, char ***out, int *amt)
{
    int len = strlen(in), i = 0, count = 0, current_len = 0, start_idx = 0;
    *out = malloc(_RRC_SPLIT_LIM);
    if (*out == NULL)
    {
        return -2;
    }

    for (; i < len; i++)
    {
        if (in[i] == by || i == (len - 1))
        {
            /* handle last element */
            if (i == (len - 1) && in[i] != by)
            {
                current_len++;
            }
            /* +1 for NULL termination */
            char *this = malloc(current_len + 1);
            if (this == NULL)
            {
                rrc_versionsfile_free_split(*out, count);
                return -1;
            }

            /* copy from offset to new array */
            memcpy(this, in + start_idx, current_len);
            this[current_len] = '\0';
            (*out)[count] = this;

            start_idx = i + 1;
            count++;
            if (amt != NULL)
                *amt = count;
            if (count > _RRC_SPLIT_LIM)
            {
                return 2;
            }

            current_len = 0;
        }
        else
        {
            current_len++;
        }
    }

    if (amt != NULL)
        *amt = count;
    return 0;
}

void rrc_versionsfile_free_split(char **array, int count)
{
    for (int i = 0; i < count; i++)
    {
        free(array[i]);
    }
    free(array);
}

struct rrc_result rrc_versionsfile_get_necessary_urls_and_versions(char *versionsfile, struct rrc_version *current_version, int *uamt, char ***urls, struct rrc_version **versions)
{
    /*
        We need to read the file line-wise and also space-wise.
        The format of the file is each line has one entry. An entry is of the form:

        version url

        Where version is a normal verstring we can parse to an int, and url is the zip
        url for that version.
        We parse each verstring, and if it yields a greater absolute value than our current version,
        we parse the url associated with it and add it to the list of updates.
    */
    *urls = malloc(_RRC_SPLIT_LIM);
    *versions = malloc(_RRC_SPLIT_LIM * sizeof(struct rrc_version));

    char **lines;
    int count;
    int res = rrc_versionsfile_split_by(versionsfile, '\n', &lines, &count);
    if (res < 0)
    {
        return rrc_result_create_error_corrupted_versionfile("Failed to split versionfile");
    }
    else if (res == 2)
    {
        rrc_versionsfile_free_split(lines, count);
        return rrc_result_create_error_corrupted_versionfile("Versionfile had more than 4096 entries");
    }

    int update_idx = 0;

    /* This is where we go through each line until we find the first one we care about and set it to first_update_idx */
    for (int i = 0; i < count; i++)
    {
        char *line = lines[i];
        char **parts;
        /* this is only here if we fail to parse it all and need to free this amount */
        int num_parts;
        int res = rrc_versionsfile_split_by(line, ' ', &parts, &num_parts);
        /* dont bother checking for 2 because it really should never ever happen */

        if (res < 0)
        {
            rrc_versionsfile_free_split(lines, count);
            return rrc_result_create_error_corrupted_versionfile("Failed to split versionfile");
        }

        struct rrc_version verint;
        struct rrc_result verstring_res = rrc_version_from_string(parts[0], &verint);

        if (rrc_result_is_error(verstring_res))
        {
            rrc_versionsfile_free_split(lines, count);
            return verstring_res;
        }

        // if the version is newer than our current version, we need to update to it
        if (rrc_version_is_older(current_version, &verint))
        {
            (*versions)[update_idx] = verint;
            (*urls)[update_idx] = parts[1];
            update_idx++;
        }
        else
        {
            free(parts[1]);
        }

        free(parts[0]);
        free(parts);
    }

    if (update_idx == 0) /* no updates needed */
    {
        *urls = NULL;
    }

    *uamt = update_idx;

    rrc_versionsfile_free_split(lines, count);
    return rrc_result_success;
}

struct rrc_result rrc_versionsfile_parse_deleted_files(char *input, struct rrc_version *current_version, struct rrc_versionsfile_deleted_file **output, int *amt)
{
    *output = malloc(sizeof(struct rrc_versionsfile_deleted_file) * _RRC_SPLIT_LIM);
    int output_idx = 0;

    char **lines;
    int count;
    int res = rrc_versionsfile_split_by(input, '\n', &lines, &count);
    if (res < 0)
    {
        return rrc_result_create_error_corrupted_versionfile("Failed to split deleted versionfile");
    }
    else if (res == 2)
    {
        rrc_versionsfile_free_split(lines, count);
        return rrc_result_create_error_corrupted_versionfile("Deleted versionfile had more than 4096 entries");
    }

    for (int i = 0; i < count; i++)
    {
        char **parts;
        int parts_count;
        res = rrc_versionsfile_split_by(lines[i], ' ', &parts, &parts_count);
        if (res < 0)
        {
            return rrc_result_create_error_corrupted_versionfile("Failed to split deleted versionfile");
        }

        struct rrc_version verint;
        struct rrc_result verstring_res = rrc_version_from_string(parts[0], &verint);

        if (rrc_result_is_error(verstring_res))
        {
            rrc_versionsfile_free_split(lines, count);
            return verstring_res;
        }

        if (rrc_version_is_older(current_version, &verint))
        {
            struct rrc_versionsfile_deleted_file file = {
                .version = verint,
                .path = parts[1]};

            (*output)[output_idx] = file;
            output_idx++;
        }
        else
        {
            free(parts[1]);
        }

        free(parts[0]);
        free(parts);
    }

    *amt = output_idx;
    rrc_versionsfile_free_split(lines, count);
    return rrc_result_success;
}
