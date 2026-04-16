/*
    loader.h - main app loader implementation header

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

#ifndef RRC_LOADER_H
#define RRC_LOADER_H

#include <string.h>

#include "../settingsfile.h"
#include <dol.h>

#define RRC_BI2_SIZE 0x2000
#define RRC_PATCH_COPY_ADDRESS 0x80900000
#define RRC_SIGNATURE_ADDRESS 0x817ffff8
#define RRC_SIGNATURE_VALUE 0xDEADBEEF

bool rrc_signature_written();

#define RRC_RUNTIME_EXT_ABI_VERSION_ADDRESS 0x817ffffc
// Must be kept in sync with runtime-ext/base.ld's PROVIDE and pulsar
#define RRC_RR_BITFLAGS 0x817ffff0
// Must be kept in sync with the .riivo_disc_ptr section address in runtime-ext's linker script
#define RRC_RIIVO_DISC_PTR 0x81782fa0
// Must be kept in sync with the .dvd_trampolines section address in runtime-ext's linker script
#define RRC_TRAMPOLINE_START 0x81782d60

// The absolute highest address that the runtime-ext DOL may use.
// We have an assert that makes sure we never copy past this.
// We subtract 64 bytes to give some wiggle room for certain
// fixed-address static data, for example at 0x817ffff0 we have the rrc_bitflags.
#define RRC_RUNTIME_EXT_DOL_SAFE_END (0x817fffff - 64)

#define RRC_RUNTIME_EXT_DOL_SAFE_START 0x81744260

// The "ABI version" of runtime-ext; incremented each time a breaking change to the ABI is necessary (different addresses etc.)
// This needs to match the ABI version declared in Code.pul or otherwise an error is emitted.
#define RRC_RUNTIME_EXT_ABI_VERSION 1

/*
    `out' should be a statically allocated string no less than 64 bytes long.
*/
void rrc_loader_get_runtime_ext_path(char region, char *out);

/*
 * This routine applies all patches from code.pul as well as setting key memory addresses
 * appropriately before fully loading the DOL and launching Mario Kart Wii.
 *
 * This function should always return a status code on failure and NEVER CRASH. On success, it never returns.
 */
void rrc_loader_load(struct rrc_dol *dol, struct rrc_settingsfile *settings, void *bi2_dest, u32 mem1_hi, u32 mem2_hi, char region);

#endif
