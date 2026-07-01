/*
    main.c - Main entry point for the runtime DOL

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

#include <stdbool.h>
#include <types.h>
#include <fcntl.h>
#include <string.h>
#include <io/fat-sd.h>
#include <rvl/cache.h>
#include <libfat/fatfile.h>
#include <riivo.h>
#include <dol.h>
#include "util.h"
#include "dvd.h"
#include "trampoline.h"
#include <errno.h>
#include <version.h>

#define EXPORT_FUNCTION(secname, decl_args, call_args, implname)       \
    __attribute__((section(secname), used)) s32 __##implname decl_args \
    {                                                                  \
        return implname call_args;                                     \
    }

EXPORT_FUNCTION(".dvd_convert_path_to_entrynum", (const char *path), (path), custom_convert_path_to_entry_num_impl);
EXPORT_FUNCTION(".dvd_open", (const char *path, FileInfo *file_info), (path, file_info), custom_open_impl);
EXPORT_FUNCTION(".dvd_fast_open", (s32 entry_num, FileInfo *file_info), (entry_num, file_info), custom_fast_open_impl);
EXPORT_FUNCTION(".dvd_read_prio", (FileInfo * file_info, void *buffer, s32 length, s32 offset, s32 prio), (file_info, buffer, length, offset, prio), custom_read_prio_impl);
EXPORT_FUNCTION(".dvd_close", (FileInfo * file_info), (file_info), custom_close_impl);

__attribute__((section(".internal_version"), used)) struct rrc_version _rrc_internal_version = RRC_INTERNAL_VERSION;

struct sd_vtable
{
    void *open;
    void *close;
    void *read;
    void *write;
    void *rename;
    void *stat;
    void *mkdir;
    void *diropen;
    void *dirnext;
    void *dirclose;
    void *seek;
    void *errno_;
};

int SD_errno()
{
    return errno;
}

__attribute__((section(".sd_vtable"), used)) static struct sd_vtable __sd_vtable = {
    .open = SD_open,
    .close = SD_close,
    .read = SD_read,
    .write = SD_write,
    .rename = SD_rename,
    .stat = SD_stat,
    .mkdir = SD_mkdir,
    .diropen = SD_diropen,
    .dirnext = SD_dirnext,
    .dirclose = SD_dirclose,
    .seek = SD_seek,
    .errno_ = SD_errno,
};

int _start()
{
    // Get the compiler to remove all unnecessary libogc deinitialization code, this function is never actually called
    while (1)
        ;
}
