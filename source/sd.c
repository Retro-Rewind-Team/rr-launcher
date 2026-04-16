/*
    sd.c - SD card helper and initialisation routines

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

#include <fat.h>
#include "unistd.h"

#include "sd.h"
#include <sys/dirent.h>
#include "update/update.h"

struct rrc_result rrc_sd_init()
{
    if (!fatInitDefault())
    {
        return rrc_result_create_error_sdcard(EIO, "Couldn't mount the SD card - is it inserted?");
    }

    if (chdir("sd:/") == -1)
    {
        return rrc_result_create_error_errno(errno, "Failed to set SD card root");
    }

    FILE *file = fopen(RRC_SD_TEST_FILE, "w+");
    if (!file)
    {
        return rrc_result_create_error_errno(errno, "The SD card is locked.");
    }

    // Test writing to the SD card, then clean up the test file.
    int res = fprintf(file, "Test");
    fflush(file);

    if (res <= 0) {
        fclose(file);
        return rrc_result_create_error_errno(errno, "The SD card is locked.");
    }
    fclose(file);

    file = fopen(RRC_VERSIONFILE, "r");
    if (file == NULL)
    {
        return rrc_result_create_error_errno(errno, "Failed to open version file.\nThis can happen if the SD card is locked,\nor your Retro Rewind installation is corrupted.");
    }

    fclose(file);
    unlink(RRC_SD_TEST_FILE);

    return rrc_result_success;
}

bool rrc_sd_file_exists(const char *path)
{
    FILE *file = fopen(path, "r");
    if (file != NULL)
    {
        fclose(file);
        return true;
    }
    else
    {
        return false;
    }
}

bool rrc_sd_folder_exists(const char *path)
{
    DIR *dir = opendir(path);
    if (dir != NULL)
    {
        closedir(dir);
        return true;
    }
    else
    {
        return false;
    }
}

int rrc_sd_get_folder_file_count(const char *path, struct rrc_result *out_err)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        *out_err = rrc_result_create_error_errno(errno, "Failed to open directory");
        return -1;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        count++;
    }

    closedir(dir);
    return count;
}

struct rrc_result rrc_sd_get_free_space(unsigned long *res)
{
    struct statvfs sbx;
    int rr = statvfs("/dev/sd", &sbx);
    if (rr != 0)
    {
        return rrc_result_create_error_errno(errno, "Failed to get free space on SD card");
    }

    *res = sbx.f_bavail * sbx.f_frsize;
    return rrc_result_success;
}