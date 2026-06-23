/*
    riivo_patch_loader.h - parsing and application of Riivolution XML patches

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

#ifndef RRC_PATCH_LOADER_H
#define RRC_PATCH_LOADER_H

#include <mxml.h>
#include <dol.h>

#include "../settingsfile.h"
#include "../result.h"

#if defined(RRC_BETA) && RRC_BETA >= 1
#define RRC_RR_MY_STUFF_PATCH_ID "RRBetaLoad"
#define RRC_CTGP_MY_STUFF_PATCH_ID "RRBetaCTGPLoad"
#define RRC_RR_MUSIC_MY_STUFF_PATCH_ID "RRBetaLoadMusic"
#define RRC_CTGP_MUSIC_MY_STUFF_PATCH_ID "RRBetaCTGPLoadMusic"
#else
#define RRC_RR_MY_STUFF_PATCH_ID "RRLoad"
#define RRC_CTGP_MY_STUFF_PATCH_ID "RRCTGPLoad"
#define RRC_RR_MUSIC_MY_STUFF_PATCH_ID "RRLoadMusic"
#define RRC_CTGP_MUSIC_MY_STUFF_PATCH_ID "RRCTGPLoadMusic"
#endif

#define MAX_FILE_PATCHES 1000
#define MAX_MEMORY_PATCHES 128
#define MAX_ENABLED_SETTINGS (64)
/// Across all folders, we cannot cache more than this value.
#define GLOBAL_MAX_FOLDER_FILES 10000

struct parse_riivo_output
{
    struct rrc_riivo_memory_patch *mem_patches;
    int mem_patches_count;
    void *loader_pul_dest;
};

/**
 * Parses <file> and <folder> patches in the XML file and gives runtime-ext a pointer to it.
 * <memory> patches are also parsed.
 * The passed `dol` is overwritten if a main.dol replacement is encountered.
 */
struct rrc_result rrc_riivo_patch_loader_parse(struct rrc_settingsfile *settings,
                                               u32 *mem1,
                                               u32 *mem2,
                                               struct rrc_dol *dol,
                                               struct parse_riivo_output *out);

#endif
