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

struct rrc_riivo_sd_file
{
    /* The SD card path to the file. */
    const char *path;
    /* Pointer to `rte_open_file`, initialized in runtime-ext when Open()-ing an entrynum */
    void *file_info;
};

struct rrc_riivo_file_replacement
{
    /* Disc-path that should be replaced */
    const char *disc;
    /* Index into `rrc_riivo_sd_file[]` */
    int entrynum;
};

struct rrc_riivo_disc
{
    struct rrc_riivo_sd_file *sd_files;
    int filename_replacements_count;
    struct rrc_riivo_file_replacement *filename_replacements;
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
