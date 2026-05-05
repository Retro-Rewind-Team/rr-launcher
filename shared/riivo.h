/*
    riivo.h - definition Riivolution types for XML parsing

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

#ifndef RRC_RIIVO_H
#define RRC_RIIVO_H

#include <dir.h>
#include "types.h"

#define RRC_RIIVO_XML_PATH "/" RRC_RETRO_REWIND_BASE_DIR "/xml/" RRC_RETRO_REWIND_BASE_DIR ".xml"

enum rrc_riivo_disc_replacement_type
{
    RRC_RIIVO_FILE_REPLACEMENT,
    RRC_RIIVO_FOLDER_REPLACEMENT,
    /// It's technically a folder replacement, but in order to save space we just use this enum.
    RRC_RIIVO_MY_STUFF_REPLACEMENT,
};

struct rrc_riivo_disc_replacement
{
    enum rrc_riivo_disc_replacement_type type;
    /// The replacement path on the SD card.
    const char *external;
    /// The path on the disc itself.
    const char *disc;
    /// If a folder, this is a list of all found files in that folder. NULL if not a folder replacement.
    /// We need to handle the edge case where we may have too many entries here, so this is not considered
    /// guaranteed to be fully populated. We just hope it can catch 99% of lookups.
    const char **folder_contents;
    /// Same as above: 0 if not a folder replacement, otherwise the number of entries in `folder_contents`.
    int folder_contents_count;
};

struct rrc_riivo_sd_file
{
    const char *path;
    void *file_info;
};

struct rrc_riivo_file_replacement
{
    const char *disc;
    int entrynum; // index into `rrc_riivo_sd_file[]`
};

struct rrc_riivo_disc
{
    struct rrc_riivo_sd_file *sd_files;
    int replacements_count;
    struct rrc_riivo_file_replacement *replacements;
};

struct rrc_riivo_memory_patch
{
    u32 addr;
    u32 value;
    u32 original; // uninitialized if !original_init
    bool original_init;
};

#endif
