/*
    loader.c - main app loader and patcher

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

#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include <wiiuse/wpad.h>
#include <ogc/conf.h>
#include <mxml.h>
#include <wiisocket.h>

#include "../gui.h"
#include "binary_loader.h"
#include "riivo_patch_loader.h"
#include "loader_addrs.h"
#include "../prompt.h"
#include "game_dol_loader.h"
#include "../util.h"
#include "../di.h"
#include "loader.h"
#include "../res.h"
#include "../console.h"
#include "../exception.h"
#include <riivo.h>
#include "../util.h"
#include "../settingsfile.h"
#include <bitflags.h>

bool rrc_signature_written()
{
    return *((u32 *)RRC_SIGNATURE_ADDRESS) == RRC_SIGNATURE_VALUE;
}

/**
 * Patches the DVD functions in the game DOL to immediately jump to custom DVD functions implemented in runtime-ext.
 * Also allocates trampolines containing the first 4 overwritten instructions + backjump to the original function,
 * which is called when the custom function wants to call the original DVD function.
 */
static void patch_dvd_functions(struct rrc_dol *dol, char region)
{
    struct function_patch_entry
    {
        // Address of the function to patch.
        u32 addr;
        // Instructions to write at the end of the trampoline. This will jump back to the original DVD function + 16 (4 instructions).
        u32 backjmp_to_original[4];
        // Instructions to overwrite the start of the original DVD function with. This will jump to the custom function.
        u32 jmp_to_custom[4];
    };

    enum rrc_dvd_region rg = rrc_region_char_to_region(region);
    if (rg == -1)
    {
        char e[64];
        snprintf(e, sizeof(e), "Unsupported region %c", region);
        struct rrc_result res = rrc_result_create_error_errno(ENOTSUP, e);
        rrc_result_error_check_error_fatal(res);
    }

    // We need to hack around the fact you can't assign to arrays unless the rhs is a constant
#define ADD_ENTRY(idx, fn)                                                         \
    struct function_patch_entry e##idx = {.addr = (*rrc_dvdf_region_addrs)[fn],    \
                                          .backjmp_to_original = {},               \
                                          .jmp_to_custom = {}};                    \
    memcpy(e##idx.backjmp_to_original, (*rrc_dvdf_region_backjmp_instrs)[fn], 16); \
    memcpy(e##idx.jmp_to_custom, rrc_dvdf_jmp_to_custom_instrs[fn], 16);           \
    entries[idx] = e##idx;

    const u32(*rrc_dvdf_region_addrs)[5] = &rrc_dvdf_addrs[(u32)rg];
    const u32(*rrc_dvdf_region_backjmp_instrs)[5][4] = &rrc_dvdf_backjmp_instrs[(u32)rg];

    struct function_patch_entry entries[5] = {};
    ADD_ENTRY(0, RRC_DVDF_CONVERT_PATH_TO_ENTRYNUM)
    ADD_ENTRY(1, RRC_DVDF_FAST_OPEN)
    ADD_ENTRY(2, RRC_DVDF_OPEN)
    ADD_ENTRY(3, RRC_DVDF_READ_PRIO)
    ADD_ENTRY(4, RRC_DVDF_CLOSE)

    for (int i = 0; i < sizeof(entries) / sizeof(struct function_patch_entry); i++)
    {
        struct function_patch_entry entry = entries[i];
        if (entry.addr == 0)
            continue;

        u32 section_index;
        void *virt_addr;
        if (!rrc_binary_find_section_by_addr(dol, entry.addr, &virt_addr, &section_index))
        {
            RRC_FATAL("Address to patch %x is not part of any game section", entry.addr);
        }

        // 32 bytes (4 instructions for the backjmp + 4 overwritten instructions restored) per patched function.
        // This is the start of the trampoline.
        u32 *hooked_addr = (u32 *)(RRC_TRAMPOLINE_START + (i * 32));
        RRC_ASSERT((u32)hooked_addr < RRC_SIGNATURE_ADDRESS, "Trampoline address overlaps with signature address");

        // Prepare the trampoline: copy the first 4 instructions of the original function that we're about to overwrite to the start,
        // and append the `backjmp_to_original` instructions.
        memcpy(hooked_addr, virt_addr, 16);
        memcpy(hooked_addr + 4, entry.backjmp_to_original, 16);
        rrc_invalidate_cache(hooked_addr, 32);

        // Overwrite the original function with a jump to the custom DVD function.
        memcpy(virt_addr, entry.jmp_to_custom, 16);
        rrc_invalidate_cache(virt_addr, 16);
    }
}

typedef void (*ic_invalidate_range_t)(void *, u32);
typedef ic_invalidate_range_t dc_flush_range_t;
typedef void (*patch_dol_func_t)(struct rrc_dol *, struct rrc_riivo_memory_patch *, int, ic_invalidate_range_t, dc_flush_range_t);

/**
 * Wrapper function around `patch_dol` that sets up the stack pointer to a safe location (workaround for missing support for __attribute__((naked))).
 */
void patch_dol_helper(
    /* r3 */ struct rrc_dol *dol,
    /* r4 */ struct rrc_riivo_memory_patch *mem_patches,
    /* r5 */ int mem_patch_count,
    /* r6 */ void (*ic_invalidate_range)(void *, u32),
    /* r7 */ void (*dc_flush_range)(void *, u32),
    /* r8 */ patch_dol_func_t);

asm("patch_dol_helper:\n"
    // Adjust the stack pointer to 0x808ffa00 (arbitrary, temporary, random safe address not used by game sections)
    // so we don't overwrite local variables while copying sections.
    "lis 9, -32625\n"
    "ori 9, 9, 64000\n"
    "mr 1,9\n"
    // Jump to the function in r8 (patch_dol). All other arguments are already in the right registers (r3-r7).
    "mtctr 8\n"
    "bctrl\n");

void rrc_loader_load(struct rrc_dol *dol, struct rrc_settingsfile *settings, void *bi2_dest, u32 mem1_hi, u32 mem2_hi, char region)
{
    struct rrc_result res;

    // runtime-ext needs to be loaded before parsing riivo patches, as it writes to a static.
    // All errors that happen here are fatal; we can't boot the game without knowing the patches or having the patched DVD functions.
    rrc_con_update("Load Runtime Extensions", 70);
    rrc_binary_load_runtime_ext(region);

    rrc_con_update("Load Patch Information", 80);
    struct parse_riivo_output riivo_out;
    res = rrc_riivo_patch_loader_parse(settings, &mem1_hi, &mem2_hi, &riivo_out);
    rrc_result_error_check_error_fatal(res);

    rrc_con_update("Patch DVD Functions", 85);
    patch_dvd_functions(dol, region);
    res = rrc_binary_load_pulsar_loader(dol, riivo_out.loader_pul_dest);
    rrc_result_error_check_error_fatal(res);

    rrc_gui_video_fix(region);

    rrc_con_update("Final Preparations", 90);
    wiisocket_deinit();

    __IOS_ShutdownSubsystems();
    for (u32 i = 0; i < 32; i++)
    {
        IOS_Close(i);
    }

    IRQ_Disable();

    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);

    // Addresses are taken from <https://wiibrew.org/wiki/Memory_map> for the most part.

    *(u32 *)0xCD006C00 = 0x00000000;              // Reset `AI_CONTROL` to fix audio
    *(u32 *)0x80000034 = 0;                       // Arena High
    *(u32 *)0x800000EC = 0x81800000;              // Dev Debugger Monitor Address
    *(u32 *)0x800000F0 = 0x01800000;              // Simulated Memory Size
    *(u32 *)0x800000F4 = (u32)bi2_dest;           // Pointer to bi2
    *(u32 *)0x800000F8 = 0x0E7BE2C0;              // Console Bus Speed
    *(u32 *)0x800000FC = 0x2B73A840;              // Console CPU Speed
    *(u32 *)0x80003110 = align_down(mem1_hi, 32); // MEM1 Arena End
    *(u32 *)0x80003124 = 0x90000800;              // Usable MEM2 Start
    *(u32 *)0x80003128 = align_down(mem2_hi, 32); // Usable MEM2 End
    *(u32 *)0x80003180 = *(u32 *)(0x80000000);    // Game ID
    *(u32 *)0x80003188 = *(u32 *)(0x80003140);    // Minimum IOS Version

    if (*(u32 *)((u32)bi2_dest + 0x30) == 0x7ED40000)
    {
        *(u8 *)0x8000319C = 0x81; // Disc is dual layer
    }
    else
    {
        *(u8 *)0x8000319C = 0x80; // Disc is single layer
    }
    ICInvalidateRange((void *)0x80000000, 0x3400);
    DCFlushRange((void *)0x80000000, 0x01800000);

    // Signature, used by Pulsar to tell that we've loaded via the new channel instead of Riivolution.
    *(u32 *)RRC_SIGNATURE_ADDRESS = RRC_SIGNATURE_VALUE;
    *(u32 *)RRC_RUNTIME_EXT_ABI_VERSION_ADDRESS = RRC_RUNTIME_EXT_ABI_VERSION;

    u8 bitflags = 0;
    if (settings->separate_savegame)
        bitflags |= RRC_BITFLAGS_SAVEGAME;
    switch (settings->my_stuff)
    {
    case RRC_SETTINGSFILE_MY_STUFF_CTGP:
        bitflags |= RRC_BITFLAGS_MY_STUFF_CTGP;
        break;
    case RRC_SETTINGSFILE_MY_STUFF_RR:
        bitflags |= RRC_BITFLAGS_MY_STUFF_RR;
        break;
    case RRC_SETTINGSFILE_MY_STUFF_CTGP_MUSIC:
        bitflags |= RRC_BITFLAGS_MY_STUFF_CTGP_MUSIC;
        break;
    case RRC_SETTINGSFILE_MY_STUFF_RR_MUSIC:
        bitflags |= RRC_BITFLAGS_MY_STUFF_RR_MUSIC;
        break;
    }

    *(u8 *)RRC_RR_BITFLAGS = bitflags;
    rrc_invalidate_cache((void *)RRC_SIGNATURE_ADDRESS, 5);

    // The last step is to copy the sections from the safe space to where they actually need to be.
    // This requires copying the function itself to the safe address space so we don't overwrite ourselves.
    // It also needs to call `DCFlushRange` but cannot reference it in the function, so we copy it and pass it as a function pointer.
    // See patch.c comment for a more detailed explanation.

    patch_dol_func_t patch_copy = (void *)RRC_PATCH_COPY_ADDRESS;

    memcpy(patch_copy, patch_dol, PATCH_DOL_LEN);
    DCFlushRange(patch_copy, align_up(PATCH_DOL_LEN, 32));
    ICInvalidateRange(patch_copy, align_up(PATCH_DOL_LEN, 32));

    ic_invalidate_range_t ic_invalidate_range = (ic_invalidate_range_t)align_up(RRC_PATCH_COPY_ADDRESS + PATCH_DOL_LEN, 32);
    memcpy(ic_invalidate_range, ICInvalidateRange, 64);
    DCFlushRange(ic_invalidate_range, 64);
    ICInvalidateRange(ic_invalidate_range, 64);

    dc_flush_range_t dc_flush_range = (dc_flush_range_t)align_up(RRC_PATCH_COPY_ADDRESS + PATCH_DOL_LEN + 64, 32);
    memcpy(dc_flush_range, DCFlushRange, 64);
    DCFlushRange(dc_flush_range, 64);
    ICInvalidateRange(dc_flush_range, 64);

    patch_dol_helper(
        dol,
        riivo_out.mem_patches,
        riivo_out.mem_patches_count,
        ic_invalidate_range,
        dc_flush_range,
        patch_copy);
}
