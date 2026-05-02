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

/**
 * Contains all <file> and <folder> replacements. Initialized in the launcher DOL based on the XML.
 */
__attribute__((section(".riivo_disc_ptr"))) static struct rrc_riivo_disc *riivo_disc = NULL;

extern u8 rrc_bitflags;

/**
 * In order to tell whether an entrynum is a special-cased SD entrynum,
 * we set a certain bit pattern in the top bits, which are very unlikely to be used
 * by real DVD entrynums.
 */
#define SPECIAL_ENTRYNUM (0b0111111101 << 22)
#define SPECIAL_ENTRYNUM_MASK (0b1111111111 << 22)

#define MAX_PATH_LEN 64
#define ENTRYNUM_SLOTS 2000
#define MAX_CONCURRENT_FILES (16)

struct rte_open_file
{
    // NB: Must be the first field, as we treat `FILE_STRUCT*` equivalently to an `rte_open_file*`.
    FILE_STRUCT file_struct;
    s32 refcount;
};

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

struct rte_sd_entrynum
{
    /**
     * In brainslug's libfat implementation, the fd is also a pointer to the
     * `FILE_STRUCT` struct (and rte_open_file as a result, by nature of it being the first field),
     * so both union fields can be accessed interchangeably.
     */
    union
    {
        s32 sd_fd;
        struct rte_open_file *opened_file;
    } file;
    char path[MAX_PATH_LEN];
    bool in_use;
};

/**
 * Stores a mapping from a path to an entrynum.
 * It can either be opened (sd_fd != 0) or closed (sd_fd == 0).
 * Opening the same entrynum multiple times will return the same fd/file_struct
 * and only increment the refcount.
 */
static struct rte_sd_entrynum sd_entrynums[ENTRYNUM_SLOTS] = {0};

/**
 * Stores additional data for an opened file. A refcount of > 0 implies that it is in use,
 * zero means that it is not. Closing a file will decrement the refcount,
 * dropping to zero will close the file and it is free to be reused.
 */
static struct rte_open_file open_files[MAX_CONCURRENT_FILES] = {0};

/**
 * Maps from a path to an entrynum. This will either be an existing entrynum
 * if it was previously converted, or a new entrynum if not.
 *
 * This is a lower level function and does not properly resolve any replacements.
 */
static s32 rte_dvd_path_to_entrynum(const char *path)
{
    int next_free_slot = -1;
    for (int i = 0; i < ENTRYNUM_SLOTS; i++)
    {
        if (!sd_entrynums[i].in_use)
        {
            next_free_slot = i;
            break;
        }

        if (strcmp(sd_entrynums[i].path, path) == 0)
        {
            // Found an entrynum for this path, return it.
            return i;
        }
    }
    if (next_free_slot == -1)
    {
        RTE_FATAL("Out of entrynum slots!");
    }

    // Path doesn't have an entrynum yet and we have a free slot, we can use it.
    sd_entrynums[next_free_slot].in_use = true;
    strncpy(sd_entrynums[next_free_slot].path, path, MAX_PATH_LEN);
    sd_entrynums[next_free_slot].file.sd_fd = 0;
    return next_free_slot;
}

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

static bool starts_with(const char *str, const char *prefix)
{
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static bool rte_dvd_resolve_my_stuff_path_to_entry_num(const char *path, s32 *entry_num)
{
    if ((rrc_bitflags & RRC_BITFLAGS_MY_STUFF_ANY) == 0)
    {
        // My Stuff not enabled.
        return false;
    }

    // If My Stuff RR/CTGP music is enabled, the path must start with /sound/strm.
    if (rrc_bitflags & RRC_BITFLAGS_MY_STUFF_ANY_MUSIC)
    {
        if (!starts_with(path, "/sound/strm"))
        {
            return false;
        }
    }

    // Save the location for faster lookups next time. The location should never change so it's worth doing.
    static int my_stuff_replacement_idx = -1;

    // Find the My Stuff replacement.
    // Work backwards since it's likely it's near the end of the list.
    struct rrc_riivo_disc_replacement *my_stuff_replacement = NULL;

    if (my_stuff_replacement_idx >= 0)
    {
        my_stuff_replacement = &riivo_disc->replacements[my_stuff_replacement_idx];
        if (my_stuff_replacement->type != RRC_RIIVO_MY_STUFF_REPLACEMENT)
        {
            RTE_FATAL("Expected My Stuff replacement at index %d, but type was %d", my_stuff_replacement_idx, my_stuff_replacement->type);
        }
    }

    if (my_stuff_replacement == NULL && my_stuff_replacement_idx != -2)
    {
        for (int i = riivo_disc->count - 1; i >= 0; i--)
        {
            if (riivo_disc->replacements[i].type == RRC_RIIVO_MY_STUFF_REPLACEMENT)
            {
                my_stuff_replacement = &riivo_disc->replacements[i];
                my_stuff_replacement_idx = i;
                break;
            }
        }
    }

    if (my_stuff_replacement == NULL)
    {
        my_stuff_replacement_idx = -2;
        RTE_DBG("My Stuff replacement entry not found in the list of replacements! This can happen if the requested folder does not exist. Skipping.\n");
        return false;
    }

    // Get the filename segment of the path: '/path/to/file.szs' -> 'file.szs'
    const char *filename = strrchr(path, '/');
    if (filename)
    {
        // Skip the slash itself.
        filename++;
    }
    else
    {
        // There's no slash in the path, so the whole path is simply the filename itself.
        filename = path;
    }

    // If My Stuff RR is enabled, look for '/RetroRewind6/MyStuff/file.szs'
    // If My Stuff CTGP is enabled, look for '/ctgpr/My Stuff/file.szs'
    char my_stuff_path[64];
    if (rrc_bitflags & (RRC_BITFLAGS_MY_STUFF_RR | RRC_BITFLAGS_MY_STUFF_RR_MUSIC))
    {
        snprintf(my_stuff_path, sizeof(my_stuff_path), "/RetroRewind6/MyStuff/%s", filename);
    }
    else
    {
        snprintf(my_stuff_path, sizeof(my_stuff_path), "/ctgpr/My Stuff/%s", filename);
    }

    bool cached_file_exists = false;
    for (int i = 0; i < my_stuff_replacement->folder_contents_count; i++)
    {
        if (strcmp(my_stuff_replacement->folder_contents[i], filename) == 0)
        {
            cached_file_exists = true;
            break;
        }
    }

    if (cached_file_exists)
    {
        RTE_DBG("Found My Stuff replacement for %s (sd path='%s')\n", filename, my_stuff_path);
        *entry_num = rte_dvd_path_to_entrynum(my_stuff_path);
        return true;
    }
    else
    {
        return false;
    }
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
 * Attempts to resolve a DVD path to an entrynum, based on the riivo file and folder replacements.
 * Returns true and writes the entrynum to `entry_num` if a replacement was found,
 * otherwise returns false.
 */
static bool rte_dvd_resolve_path_to_entry_num(const char *filename, s32 *entry_num)
{
    rrc_rt_sd_init();

    // Try My Stuff replacements first.
    if (rte_dvd_resolve_my_stuff_path_to_entry_num(filename, entry_num))
    {
        return true;
    }

    for (int i = riivo_disc->count - 1; i >= 0; i--)
    {
        const struct rrc_riivo_disc_replacement *replacement = &riivo_disc->replacements[i];
        switch (replacement->type)
        {
        case RRC_RIIVO_FILE_REPLACEMENT:
        {
            RTE_DBG("Checking file replacement: '%s' == '%s'\n", replacement->disc, "strm");

            // Trim leading slashes from either path.
            const char *disc_path = replacement->disc;
            if (*disc_path == '/')
            {
                disc_path++;
            }
            const char *ffilename = filename;
            if (*ffilename == '/')
            {
                ffilename++;
            }

            if (strcmp(disc_path, ffilename) == 0)
            {
                // We already checked that the external file exists when we registered the replacement.
                RTE_DBG("Found a file replacement! %d (%s)\n", i, disc_path);
                *entry_num = rte_dvd_path_to_entrynum(replacement->external);
                return true;
            }
            break;
        }
        case RRC_RIIVO_FOLDER_REPLACEMENT:
        {
            const char *external_path = replacement->external;
            const char *disc_path = replacement->disc;

            int disc_len = strlen(disc_path);
            int external_len = strlen(external_path);
            int filename_len = strlen(filename);
            if (disc_len > filename_len)
            {
                break;
            }

            // Check if this folder path is a prefix of the given filename (`matches`),
            // and if it is, find the "split" point at which they differ (`differ_index`). Example:
            // Game requests "Assets/RaceAssets.szs", folder replacement is "/Assets" -> "/CustomAssets".
            // This matches (despite a leading / in only one of the paths), and `differ_index` is the index of the `/`.
            // Everything after that index is append to the external path: "/CustomAssets" + "/RaceAssets.szs"
            // is resolved to "/CustomAssets/RaceAssets.szs".
            // Note that the "disc" path ends at a folder so the disc path should always be shorter than the filename,
            // which is a full, absolute path to the file.
            bool matches = true;
            int differ_index = 0;
            for (int di = 0; di < disc_len; di++)
            {
                // NB: filename_len >= disc_len, so any `di` is also valid for `filename`
                if (di == 0 && disc_path[0] == '/' && filename[0] != '/')
                {
                    // No explicit / in the requested filename. Allow this.
                    continue;
                }
                if (tolower((unsigned char)disc_path[di]) != tolower((unsigned char)filename[differ_index]))
                {
                    matches = false;
                    break;
                }
                differ_index++;
            }

            RTE_DBG("Found folder rename: '%s' == '%s' -> %d %d\n", disc_path, filename, matches, differ_index);

            if (matches)
            {
                // The folder replacement path matches. Let's see if the file actually exists in the directory.
                char new_path[64];
                char *path_ptr = new_path;
                if (strlen(external_path) >= 64)
                {
                    RTE_FATAL("External path '%s' is too long", external_path);
                }
                strncpy(new_path, external_path, sizeof(new_path));

                path_ptr += external_len;
                if (filename[differ_index] != '/' && external_path[external_len - 1] != '/')
                {
                    // Add a / if there isn't already one that would separate the two paths.
                    *path_ptr = '/';
                    path_ptr++;
                }
                strncpy(path_ptr, filename + differ_index, 64 - ((u32)path_ptr - (u32)new_path));

                RTE_DBG("Checking for folder replacement path '%s' (external_path='%s', filename='%s', disc_path='%s')\n", new_path, external_path, filename, disc_path);

                // `path_ptr` (and `new_path_filename`) now points right after the common prefix (so usually the filename),
                // which we will use to check against the folder contents.
                // But first, there might be a `/` here still for e.g. `disc=/b requested=/b/c`:
                // new_path_filename` will point at `/c` (differ_index=2), but we only want `c`, so skip it.
                char *new_path_filename = path_ptr;
                if (*new_path_filename == '/')
                {
                    new_path_filename++;
                }

                bool cached_file_exists = false;
                for (int i = 0; i < replacement->folder_contents_count; i++)
                {
                    // We need to enforce case insensitivity here because FAT is case-insensitive,
                    // and the folder_contents are populated based on FAT reads.

                    to_lowercase((char *)replacement->folder_contents[i]);
                    to_lowercase(new_path_filename);

                    if (strcmp(replacement->folder_contents[i], new_path_filename) == 0)
                    {
                        RTE_DBG("Found a cached match for the filename in the folder contents!\n");
                        cached_file_exists = true;
                        break;
                    }
                }

                if (cached_file_exists)
                {
                    RTE_DBG("Found a folder replacement! %d (%s %s %s %s) (cached=%s)\n", i, disc_path, external_path, filename, new_path, cached_file_exists ? "true" : "false");
                    *entry_num = rte_dvd_path_to_entrynum(new_path);
                    return true;
                }
                else
                {
                    OS_Report("NOTE: %s/%s not applied because it doesn't exist.\n", external_path, new_path_filename);
                }
            }

            break;
        }
        case RRC_RIIVO_MY_STUFF_REPLACEMENT:
            // My Stuff replacements are handled separately in `rte_dvd_resolve_my_stuff_path_to_entry_num`, so we can skip them here.
            break;
        }
    }

    return false;
}

/**
 * Opens a resolved entrynum and fills the `FileInfo` pointer.
 */
static void rte_dvd_open_entry_num(s32 entry_num, FileInfo *file_info)
{
    struct rte_sd_entrynum *etp = &sd_entrynums[entry_num];

    if (etp->file.sd_fd != 0)
    {
        RTE_DBG("FastOpen: reusing fd %d\n", etp->file.sd_fd);
        file_info->startAddr = SPECIAL_ENTRYNUM | entry_num;
        file_info->length = etp->file.opened_file->file_struct.filesize;
        etp->file.opened_file->refcount++;
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

        if (fd != (u32)file)
        {
            RTE_FATAL("Broken assumption: SD_open() fd is not the same as the file pointer!");
        }

        etp->file.opened_file = file;

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
        int slot = file_info->startAddr & ~SPECIAL_ENTRYNUM_MASK;

        struct rte_sd_entrynum *etp = &sd_entrynums[slot];
        if (!etp->in_use)
        {
            RTE_FATAL("ReadPrio: uninitialized slot!\n");
        }

        if (etp->file.sd_fd == 0)
        {
            RTE_FATAL("ReadPrio: file is already closed!\n");
        }

        if (SD_seek(etp->file.sd_fd, offset, 0) == -1)
        {
            RTE_FATAL("ReadPrio: Failed to seek (%d)\n", errno);
        }

        int bytes = SD_read(etp->file.sd_fd, buffer, length);
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
        struct rte_sd_entrynum *etp = &sd_entrynums[file_info->startAddr & ~SPECIAL_ENTRYNUM_MASK];

        if (!etp->in_use)
        {
            RTE_FATAL("Attempted to close slot that is uninitialized!");
        }

        if (etp->file.sd_fd == 0)
        {
            RTE_FATAL("Close: file is already closed!\n");
            return 1;
        }

        // NB: sd_fd != 0, so the file is definitely open.

        if (etp->file.opened_file->refcount == 0)
        {
            RTE_FATAL("BUG: refcount should never be 0 for open files.");
        }

        etp->file.opened_file->refcount--;
        if (etp->file.opened_file->refcount == 0)
        {
            if (SD_close(etp->file.sd_fd) == -1)
            {
                RTE_FATAL("Failed to close SD file due to SD error (%d)", errno);
            }

            etp->file.sd_fd = 0;
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
