/*
    dvd.c - DVD replacement function implementations

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

#include <types.h>
#include <fcntl.h>
#include "sd.h"
#include "dvd.h"
#include "util.h"
#include <io/fat.h>
#include <io/fat-sd.h>
#include <errno.h>
#include <stdio.h>
#include <rvl/cache.h>
#include <riivo.h>
#include <bitflags.h>
#include "trampoline.h"
#include <string.h>
#include <ctype.h>
#include <dir.h>

/**
 * Contains pointers to the file replacements and SD file entries.
 */
__attribute__((section(".riivo_disc_ptr"))) static struct rrc_riivo_disc riivo_disc = {0};

extern u8 rrc_bitflags;

/**
 * In order to tell whether an entrynum is a special-cased SD entrynum,
 * we set a certain bit pattern in the top bits, which are very unlikely to be used
 * by real DVD entrynums.
 */
#define SPECIAL_ENTRYNUM (0b0111111101 << 22)
#define SPECIAL_ENTRYNUM_MASK (0b1111111111 << 22)

#define MAX_PATH_LEN 64
#define MAX_CONCURRENT_FILES (16)

struct rte_open_file
{
    // NB: Must be the first field, as we treat `FILE_STRUCT*` equivalently to an `rte_open_file*`.
    FILE_STRUCT file_struct;
    s32 refcount;
};

/** Returns the file descriptor of an open file which can be passed to the SD_xxx() functions. */
static int rte_riivo_sd_fd(struct rrc_riivo_sd_file *sd_file)
{
    if (sd_file->file_info == NULL)
    {
        RTE_FATAL("Attempted to get fd of closed file!");
    }

    // The fd is simply the pointer to the FILE_STRUCT
    return (u32)(&((struct rte_open_file *)sd_file->file_info)->file_struct);
}

// Size/Align assumptions made by RR/Pulsar's SDIO
_Static_assert(sizeof(FILE_STRUCT) == 80);
_Static_assert(_Alignof(FILE_STRUCT) == 4);
_Static_assert(sizeof(DIR_STATE_STRUCT) == 836);
_Static_assert(_Alignof(DIR_STATE_STRUCT) == 4);
_Static_assert(sizeof(struct stat) == 88);
_Static_assert(_Alignof(struct stat) == 8);

_Static_assert(offsetof(struct stat, st_mode) == 8);
_Static_assert(offsetof(struct stat, st_nlink) == 12); // Make sure stat is a u32
_Static_assert(offsetof(FILE_STRUCT, filesize) == 0);

_Static_assert(S_IFDIR == 0040000);
_Static_assert(S_IFMT == 0170000);

/**
 * Stores additional data for an opened file. A refcount of > 0 implies that it is in use,
 * zero means that it is not. Closing a file will decrement the refcount,
 * dropping to zero will close the file and it is free to be reused.
 */
static struct rte_open_file open_files[MAX_CONCURRENT_FILES] = {0};

/**
 * Allocates a slot for the opened files array.
 */
static struct rte_open_file *rte_dvd_alloc_open_file()
{
    for (int i = 0; i < MAX_CONCURRENT_FILES; i++)
    {
        if (open_files[i].refcount == 0)
        {
            open_files[i].refcount = 1;
            return &open_files[i];
        }
    }
    RTE_FATAL("Attempted to open more than " RTE_STRINGIFY(MAX_CONCURRENT_FILES) " SD files at once!");
}

char *strstr1(const char *s1, const char *s2)
{
    size_t n = strlen(s2);
    while (*s1)
        if (!memcmp(s1++, s2, n))
            return (char *)(s1 - 1);
    return 0;
}

/**
 * Checks if two strings are equal, ignoring case.
 */
bool strieq(const char *a, const char *b)
{
    int a_len = strlen(a), b_len = strlen(b);
    if (a_len != b_len)
        return false;

    for (int i = 0; i < a_len; i++)
    {
        if (tolower(a[i]) != tolower(b[i]))
            return false;
    }
    return true;
}

/**
 * Attempts to resolve a DVD path to an entrynum, based on the riivo file and folder replacements.
 * Returns true and writes the entrynum to `entry_num` if a replacement was found,
 * otherwise returns false.
 */
static bool rte_dvd_resolve_path_to_entry_num(const char *filename, s32 *entry_num)
{
    rrc_rt_sd_init();

    for (int i = riivo_disc.filename_replacements_count - 1; i >= 0; i--)
    {
        const struct rrc_riivo_file_replacement *replacement = &riivo_disc.filename_replacements[i];
        const char *filename_segment = strrchr(filename, '/');
        if (filename_segment)
        {
            filename_segment++; // Skip the '/'
        }
        else
        {
            filename_segment = filename;
        }
        RTE_DBG("Checking filename replacement: '%s' == '%s'\n", replacement->disc, filename_segment);

        if (strieq(replacement->disc, filename_segment))
        {
            // We already checked that the external file exists when we registered the replacement.
            RTE_DBG("Found a filename replacement! %d (%s -> %s)\n", i, replacement->disc, riivo_disc.sd_files[replacement->entrynum].path);
            *entry_num = replacement->entrynum;
            return true;
        }
    }

    for (int i = riivo_disc.replacements_count - 1; i >= 0; i--)
    {
        const struct rrc_riivo_file_replacement *replacement = &riivo_disc.replacements[i];
        RTE_DBG("Checking file replacement: '%s' == '%s'\n", replacement->disc, filename);

        // Trim leading slashes from either path.
        const char *disc_path = replacement->disc;
        if (*disc_path == '/')
        {
            disc_path++;
        }
        if (*filename == '/')
        {
            filename++;
        }

        if (strieq(disc_path, filename))
        {
            // We already checked that the external file exists when we registered the replacement.
            RTE_DBG("Found a file replacement! %d (%s -> %s)\n", i, disc_path, riivo_disc.sd_files[replacement->entrynum].path);
            *entry_num = replacement->entrynum;
            return true;
        }
    }

    return false;
}

/**
 * Opens a resolved entrynum and fills the `FileInfo` pointer.
 */
static void rte_dvd_open_entry_num(s32 entry_num, FileInfo *file_info)
{
    struct rrc_riivo_sd_file *etp = &riivo_disc.sd_files[entry_num];

    if (etp->file_info != NULL)
    {
        struct rte_open_file *opened_file = (struct rte_open_file *)etp->file_info;
        RTE_DBG("FastOpen: reusing entrynum %d\n", entry_num);
        file_info->startAddr = SPECIAL_ENTRYNUM | entry_num;
        file_info->length = opened_file->file_struct.filesize;
        opened_file->refcount++;
    }
    else
    {
        struct rte_open_file *file = rte_dvd_alloc_open_file();

        int fd = SD_open(&file->file_struct, etp->path, O_RDONLY);
        RTE_DBG("Open path '%s', fd = %d\n", etp->path, fd);

        if (fd == -1)
        {
            RTE_FATAL("FastOpen: SD error!\n\nOpen path '%s'\nfailed with error %d\n", etp->path, errno);
        }

        if (fd != (u32)(&file->file_struct))
        {
            RTE_FATAL("Broken assumption: SD_open() fd is not the same as the file pointer!");
        }

        etp->file_info = file;

        file_info->startAddr = SPECIAL_ENTRYNUM | entry_num;
        file_info->length = file->file_struct.filesize;
    }
}

////////////////////////////
// Replaced DVD functions //
////////////////////////////

// The replaced DVD functions (defined in main.c) are defined with a custom section
// so that we can give it a special address in a linker script, and immediately calls a function suffixed with `_impl` implemented here,
// marked with `__attribute__((noinline))`.
// The reason for this is so that the function that has a fixed address will always be very small (1 call instruction, so 4 bytes),
// and we don't need to worry about constantly having to update the addresses. The `_impl` functions can live in the big .text section.

__attribute__((noinline))
s32
custom_convert_path_to_entry_num_impl(const char *filename)
{
    RTE_DBG("ConvertPathToEntrynum(%s)\n", filename);

    s32 entry_num;
    if (rte_dvd_resolve_path_to_entry_num(filename, &entry_num))
    {
        RTE_DBG("Found entrynum replacement: %d\n", entry_num);
        return SPECIAL_ENTRYNUM | entry_num;
    }

    // Return to original overwritten function
    s32 res = dvd_convert_path_to_entrynum_trampoline(filename);
    if ((res & SPECIAL_ENTRYNUM_MASK) == SPECIAL_ENTRYNUM)
    {
        RTE_FATAL("DVD Convert path returned special entry (%d)", res);
    }
    else
    {
        return res;
    }
}

__attribute__((noinline))
s32
custom_open_impl(const char *path, FileInfo *file_info)
{
    RTE_DBG("Open(%s)\n", path);

    s32 entry_num;
    if (rte_dvd_resolve_path_to_entry_num(path, &entry_num))
    {
        rte_dvd_open_entry_num(entry_num, file_info);
        RTE_DBG("Found entrynum replacement: %d (addr %d)\n", entry_num, file_info->startAddr);
        return 1;
    }

    s32 res = dvd_open_trampoline(path, file_info);
    RTE_DBG("Default DVD Open (%d) address: %d\n", res, file_info->startAddr);
    return res;
}

__attribute__((noinline))
s32
custom_fast_open_impl(s32 entry_num, FileInfo *file_info)
{
    RTE_DBG("FastOpen(%d)\n", entry_num);

    if ((entry_num & SPECIAL_ENTRYNUM_MASK) == SPECIAL_ENTRYNUM)
    {
        rte_dvd_open_entry_num(entry_num & ~SPECIAL_ENTRYNUM_MASK, file_info);
        return 1;
    }

    // Return to original overwritten function
    s32 res = dvd_fast_open_trampoline(entry_num, file_info);
    if (res != -1 && (file_info->startAddr & SPECIAL_ENTRYNUM_MASK) == SPECIAL_ENTRYNUM)
    {
        RTE_FATAL("Normal FastOpen() returned special bitpattern (%d)", res);
    }
    return res;
}

__attribute__((noinline))
s32
custom_read_prio_impl(FileInfo *file_info, void *buffer, s32 length, s32 offset, s32 prio)
{
    RTE_DBG("ReadPrio(%x, %d, %d) (startAddr=%d,size=%d)\n", buffer, length, offset, file_info->startAddr, file_info->length);

    if ((file_info->startAddr & SPECIAL_ENTRYNUM_MASK) == SPECIAL_ENTRYNUM)
    {
        int entrynum = file_info->startAddr & ~SPECIAL_ENTRYNUM_MASK;

        struct rrc_riivo_sd_file *etp = &riivo_disc.sd_files[entrynum];
        if (etp->file_info == NULL)
        {
            RTE_FATAL("ReadPrio: attempted to read from file before it is opened!\n");
        }

        int fd = rte_riivo_sd_fd(etp);

        if (SD_seek(fd, offset, 0) == -1)
        {
            RTE_FATAL("ReadPrio: Failed to seek (%d)\n", errno);
        }

        int bytes = SD_read(fd, buffer, length);
        if (bytes == -1)
        {
            RTE_FATAL("ReadPrio: failed to read bytes in ReadPrio (%d)", errno);
        }

        DCFlushRange(buffer, align_up(length, 32));
        ICInvalidateRange(buffer, align_up(length, 32));
        return bytes;
    }

    return dvd_read_prio_trampoline(file_info, buffer, length, offset, prio);
}

__attribute__((noinline)) bool
custom_close_impl(FileInfo *file_info)
{
    RTE_DBG("Close(%d)\n", file_info->startAddr);

    if ((file_info->startAddr & SPECIAL_ENTRYNUM_MASK) == SPECIAL_ENTRYNUM)
    {
        int entrynum = file_info->startAddr & ~SPECIAL_ENTRYNUM_MASK;
        struct rrc_riivo_sd_file *etp = &riivo_disc.sd_files[entrynum];

        if (etp->file_info == NULL)
        {
            RTE_FATAL("Attempted to close file that is not open!");
        }

        struct rte_open_file *opened_file = (struct rte_open_file *)etp->file_info;

        if (opened_file->refcount == 0)
        {
            RTE_FATAL("BUG: refcount should never be 0 for open files.");
        }

        opened_file->refcount--;
        if (opened_file->refcount == 0)
        {
            if (SD_close(rte_riivo_sd_fd(etp)) == -1)
            {
                RTE_FATAL("Failed to close SD file due to SD error (%d)", errno);
            }

            etp->file_info = NULL;
        }

        return 1;
    }

    // Why this calls DVD_Cancel() instead of DVD_Close() you may wonder?
    // `DVDClose()` immediately has a relative branch to `DVDCancel()` as the first instruction.
    // We can't execute that relative jump in the copied trampoline,
    // but we know the absolute address of `DVDCancel()`, so we can just call it directly here.
    //
    // And yes: `DVDClose()` really always returns true (it has to!), the game's DVD error handler
    // has a bug where it will use-after-free in GP mode.

    // We discard the return value of DVDCancel just in case by some force of God it doesn't return true.
    DVDCancel(file_info);
    return true;
}
