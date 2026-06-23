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
#include <hash.h>

/**
 * Contains pointers to the file replacements and SD file entries.
 */
__attribute__((section(".riivo_disc"))) static struct rrc_riivo_disc riivo_disc = {0};

extern u8 rrc_bitflags;

/**
 * In order to tell whether an entrynum is a special-cased SD entrynum,
 * we set a certain bit pattern in the top bits, which are very unlikely to be used
 * by real DVD entrynums.
 */
#define SPECIAL_ENTRYNUM (0b0111111101 << 22)
#define SPECIAL_ENTRYNUM_MASK (0b1111111111 << 22)

#define MAX_CONCURRENT_FILES (16)

struct rte_open_file
{
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
 *
 * Importantly, this static is zero-initialized such that each refcount starts at 0 and is used by alloc_open_file().
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

/**
 * Searches for a file replacement by just the hash of the normalized disc path.
 * Normalized here means that leading slashes need to be removed and the path is lowercased.
 *
 * **NOTE** that due to hash collision, the returned int may not immediately be the correct replacement:
 * the caller needs to walk forwards from the returned index as long as the hash still matches and compare the disc_path.
 */
static int rte_dvd_search_replacement_by_hash(u32 hash, struct rrc_riivo_file_replacement *replacements, int replacements_len)
{
    // Replacements are sorted by their hash, so we can do a binary search here.
    int start = 0, end = replacements_len;

    while (start < end)
    {
        int mid = (start + end) / 2;
        u32 mid_hash = replacements[mid].hash;
        if (mid_hash == hash)
        {
            // We have a match we can return.
            //
            // Edge case: if we have multiple replacements with the same hash, return the index of the first one (`mid` may be in the middle of multiple entries with the same hash).
            // Consider for example the list: [1 2 2 2 3]; if we land...
            //                                     ^ ...here, then the caller would need to check backwards and forwards for the correct one.
            // So instead, we walk backwards to the first same hash so the caller only needs to check forwards.
            while (mid > 0 && replacements[mid - 1].hash == hash)
            {
                mid--;
            }

            return mid;
        }
        else if (mid_hash < hash)
        {
            start = mid + 1;
        }
        else
        {
            end = mid;
        }
    }
    return -1;
}

/**
 * Searches for the matching replacement in the given replacements array. This takes into account hash collisions.
 */
static bool rte_dvd_search_replacement(u32 hash, const char *filename, struct rrc_riivo_file_replacement *replacements, int replacements_len, s32 *entry_num)
{
    int index = rte_dvd_search_replacement_by_hash(hash, replacements, replacements_len);
    if (index != -1)
    {
        // We found a replacement by hash. Now find the correct one by comparing paths.
        for (int i = index; i < replacements_len && replacements[i].hash == hash; i++)
        {
            const struct rrc_riivo_file_replacement *replacement = &replacements[i];
            if (strcicmp(replacement->disc, filename) == 0)
            {
                RTE_DBG("Found a file replacement! %d (%s -> %s)\n", i, replacement->disc, riivo_disc.sd_files[replacement->entrynum].path);
                *entry_num = replacement->entrynum;
                return true;
            }
        }
    }
    return false;
}

/**
 * Attempts to resolve a DVD path to an entrynum, based on the riivo file replacements.
 * Returns true if a replacement was found, otherwise returns false.
 */
static bool rte_dvd_resolve_path_to_entry_num(const char *filename, s32 *entry_num)
{
    rrc_rt_sd_init();

    if (filename[0] == '/')
    {
        filename++; // Normalization: trim leading slashes, both normal replacements and filename replacements are registered without leading slashes.
    }

    const char *filename_segment = strrchr(filename, '/');
    if (filename_segment)
    {
        filename_segment++; // Skip the '/'
    }
    else
    {
        filename_segment = filename;
    }
    u32 full_hash = rrc_hash_string_lowercase(filename);
    u32 filename_hash = rrc_hash_string_lowercase(filename_segment);

    // Look for filename search replacements first.
    if (rte_dvd_search_replacement(filename_hash, filename_segment, riivo_disc.filename_replacements, riivo_disc.filename_replacements_count, entry_num))
    {
        return true;
    }

    // Look for normal replacements.
    if (rte_dvd_search_replacement(full_hash, filename, riivo_disc.replacements, riivo_disc.replacements_count, entry_num))
    {
        return true;
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
