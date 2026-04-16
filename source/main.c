/*
    main.c - entry point and key init routines

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

#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <unistd.h>
#include <ogc/wiilaunch.h>
#include <string.h>
#include <fat.h>
#include <curl/curl.h>
#include <mxml.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <dirent.h>
#include <bitflags.h>

#include "loader/disc_loader.h"
#include "shutdown.h"
#include "util.h"
#include "di.h"
#include "time.h"
#include "loader/loader.h"
#include <dol.h>
#include <riivo.h>
#include "console.h"
#include "settings.h"
#include "update/versionsfile.h"
#include "update/update.h"
#include "prompt.h"
#include "gui.h"
#include "res.h"
#include "settingsfile.h"
#include "result.h"
#include "exception.h"
#include "sd.h"
#include "pad.h"
#include "crash.h"
#include "ephfile.h"

/* 100ms */
#define DISKCHECK_DELAY 100000

int main(int argc, char **argv)
{
    // There are bugs in pulsar with USB/HIDv5 if the IOS version is 59, which HBC commonly boots programs with.
    // Use a version used by the game that is known to work with pulsar.
    // FIXME: try to use the disk's IOS version?
    RRC_ASSERTEQ(IOS_ReloadIOS(37), 0, "Failed to reload IOS");

    rrc_shutdown_register_callbacks();

    // We reserve ~1MB of MEM1 upfront for the runtime-ext dol.
    u32 mem1_hi = RRC_RUNTIME_EXT_DOL_SAFE_START;
    u32 mem2_hi = *(u32 *)0x80003128;

    s64 systime_start = gettime();

    // response codes for various library functions
    int res;

    void *xfb;
    // init video, setup console framebuffer
    rrc_gui_xfb_alloc(&xfb, false);
    rrc_gui_display_con(xfb, true);
    rrc_gui_display_banner(xfb);

    init_exception_handlers();

    // NOTE: We can't call any kind of printf before initialising libfat
    struct rrc_result sdinit_res = rrc_sd_init();
    rrc_result_error_check_error_fatal(sdinit_res);

    errno = 0;

    DIR *dir = opendir("sd:/RetroRewindChannel");
    if (dir != NULL)
    {
        closedir(dir);
    }
    else
    {
        // ???
        struct rrc_result err = rrc_result_create_error_errno(errno, "Failed to open sd:/RetroRewindChannel");
        rrc_result_error_check_error_fatal(err);
    }

    rrc_con_update("Initialise controllers", 0);
    res = PAD_Init();
    RRC_ASSERTEQ(res, 1, "PAD_Init");
    res = WPAD_Init();
    RRC_ASSERTEQ(res, WPAD_ERR_NONE, "WPAD_Init");

    bool first_open = false;

    FILE *afd = fopen("sd:/RetroRewindChannel/accept.txt", "r");
    if (afd == NULL)
    {
        char *lines[] = {
            "Welcome to the new (beta) Retro Rewind Channel!",
            "",
            "You will now be taken to the settings menu.",
            "Please make sure your settings are the same",
            "as what you used with the old channel.",
            "",
            "",
            "By continuing, you acknowledge that:",
            "1. The channel is in a beta and therefore unfinished state,",
            "2. The channel comes with NO WARRANTY, express or implied,",
            "3. Taking a backup of your SD card is recommended."
        };

        enum rrc_prompt_result result = rrc_prompt_2_options(xfb, lines, 11, "I Accept", "Close Launcher", RRC_PROMPT_RESULT_OK, RRC_PROMPT_RESULT_CANCEL);
        if (result == RRC_PROMPT_RESULT_CANCEL)
        {
            exit(0);
        }

        if(result == RRC_PROMPT_RESULT_ERROR) {
            struct rrc_result err = rrc_result_create_error_errno(EIO, "Failed to display prompt");
            rrc_result_error_check_error_normal(err, xfb);
        }

        FILE *afd = fopen("sd:/RetroRewindChannel/accept.txt", "w");
        if (afd == NULL)
        {
            struct rrc_result err = rrc_result_create_error_errno(errno, "Failed to create acceptance file. The SD card may be write locked.");
            rrc_result_error_check_error_normal(err, xfb);
        }
        else
        {
            fclose(afd);
        }

        first_open = true;
    }
    else
    {
        fclose(afd);
    }

    rrc_con_update("Load settings", 5);
    struct rrc_settingsfile stored_settings;
    struct rrc_result settingsfile_res = rrc_settingsfile_parse(&stored_settings);
    if (rrc_result_is_error(settingsfile_res))
    {
        char *lines[] = {
            rrc_result_strerror(settingsfile_res),
            (char *)rrc_result_context(settingsfile_res),
            "It may be possible to fix this by recreating the file.",
            "Recreate now?",
        };
        rrc_result_free(settingsfile_res);

        enum rrc_prompt_result prompt_res = rrc_prompt_yes_no(xfb, lines, 4);

        if (prompt_res == RRC_PROMPT_RESULT_YES)
        {
            settingsfile_res = rrc_settingsfile_create();
            if (rrc_result_is_error(settingsfile_res))
            {
                char *lines[] = {
                    "Failed to recreate settings file.",
                    rrc_result_strerror(settingsfile_res),
                    (char *)rrc_result_context(settingsfile_res),
                    "Defaults will be used with no changes on the SD card.",
                };
                rrc_prompt_1_option(xfb, lines, 4, "OK");
            }
            rrc_result_free(settingsfile_res);
        }

        // `rrc_settingsfile_parse()` always initializes the settingsfile, so even in case of an error here,
        // it is initialized with defaults and we can continue with that.
    }

    bool crashed = false;
    if(rrc_launched_after_crash())
    {
        crashed = true;
        rrc_crash_handle(xfb, &stored_settings);
    }

    rrc_con_update("Initialise DVD", 10);
    int fd = rrc_di_init();
    RRC_ASSERT(fd != 0, "rrc_di_init");

    rrc_con_update("Initialise DVD: Check for Mario Kart Wii", 12);
    /*  We should load Mario Kart Wii before doing anything else */
    char region;
    res = rrc_disc_loader_await_mkw(xfb, &region);
    if (res == RRC_RES_SHUTDOWN_INTERRUPT)
    {
        exit(0);
    }

    // We've identified the game. Now find the data partition, which will tell us where the DOL and FST is.
    // This first requires parsing the partition *groups*. Each partition group contains multiple partitions.
    // Data partitions have the id 0.

    rrc_con_update("Initialise DVD: Load Data Partition", 15);
    struct rrc_di_part_group part_groups[4] __attribute__((aligned(32)));
    res = rrc_di_unencrypted_read(&part_groups, sizeof(part_groups), RRC_DI_PART_GROUPS_OFFSET >> 2);
    RRC_ASSERTEQ(res, RRC_DI_LIBDI_OK, "rrc_di_unencrypted_read for partition group");

    u32 data_part_offset = UINT32_MAX;
    res = rrc_disc_loader_locate_data_part(&data_part_offset);

    if (data_part_offset == UINT32_MAX)
    {
        RRC_FATAL("no data partition found on disk");
    }
    rrc_dbg_printf("data partition found at offset %x\n", data_part_offset << 2);

    rrc_con_update("Initialise DVD: Read Data Partition", 17);

    res = rrc_di_open_partition(data_part_offset);
    RRC_ASSERTEQ(res, RRC_DI_LIBDI_OK, "rrc_di_open_partition");

    struct rrc_di_data_part_header data_header[3] __attribute__((aligned(32)));
    res = rrc_di_read(&data_header, sizeof(data_header), 0x420 >> 2);
    RRC_ASSERTEQ(res, RRC_DI_LIBDI_OK, "rrc_di_read data partition header");

    rrc_dbg_printf("DOL offset: %d\n", data_header->dol_offset << 2);
    rrc_dbg_printf("FST offset: %d\n", data_header->fst_offset << 2);
    rrc_dbg_printf("FST size: %d\n", data_header->fst_size << 2);

    bool loaded_from_rr = rrc_launched_from_rr();
    // Check for updates if the user enabled that setting. Skip if we loaded from the game since this will have already ran.
    if (stored_settings.auto_update && !loaded_from_rr && !crashed)
    {

        int update_count;
        bool any_updates;
        struct rrc_result update_res = rrc_update_do_updates(xfb, &update_count, &any_updates);
        rrc_result_error_check_error_normal(update_res, xfb);
    }

#define INTERRUPT_TIME 5000000 /* 5 seconds */
    rrc_con_clear(true);

    if(!first_open) {
        rrc_con_print_text_centered(_RRC_ACTION_ROW, "Press A to launch, or press B to load settings.");
        rrc_con_print_text_centered(_RRC_ACTION_ROW + 1, "Auto-launching in 5 seconds...");
    }

    for (int i = 0; i < INTERRUPT_TIME / RRC_WPAD_LOOP_TIMEOUT; i++)
    {
        rrc_shutdown_check();

        struct pad_state pad = rrc_pad_buttons();

        if (rrc_pad_home_pressed(pad))
        {
            return 0;
        }
        else if (rrc_pad_a_pressed(pad))
        {
            break;
        }
        // Only allow auto launch if this is a successful first boot
        else if (rrc_pad_b_pressed(pad) || first_open || loaded_from_rr || crashed)
        {
            struct rrc_result r;
            int out = rrc_settings_display(xfb, &stored_settings, &r);
            rrc_result_error_check_error_fatal(r);

            switch (out)
            {
            case RRC_SETTINGS_LAUNCH:
                goto interrupt_loop_end;
            case RRC_SETTINGS_EXIT:
                return 0;
            }
        }

        usleep(RRC_WPAD_LOOP_TIMEOUT);
    }
interrupt_loop_end:

    rrc_con_clear(true);

    rrc_con_update("Initialise DVD: Read Game DOL", 25);

    // read dol
    struct rrc_dol *dol = (struct rrc_dol *)0x80901000;
    res = rrc_di_read(dol, sizeof(struct rrc_dol), data_header->dol_offset);
    RRC_ASSERTEQ(res, RRC_DI_LIBDI_OK, "rrc_di_read for dol");

    rrc_dbg_printf("Entrypoint at %x\n", dol->entry_point);
    rrc_dbg_printf("BSS Addr: %x\n", dol->bss_addr);
    rrc_dbg_printf("BSS size: %d\n", dol->bss_size);
    for (u32 i = 0; i < RRC_DOL_SECTION_COUNT; i++)
    {
        if (dol->section_size[i] == 0)
        {
            continue;
        }
        rrc_dbg_printf("%x at %x-%x (%d b)\n", dol->section[i], dol->section_addr[i], dol->section_addr[i] + dol->section_size[i], dol->section_size[i]);
        if ((dol->section_addr[i] < 0x80000000) || (dol->section_addr[i] + dol->section_size[i] > 0x90000000))
        {
            RRC_FATAL("Invalid section address: %x", dol->section_addr[i]);
        }

        // See patch.c comment for why we first copy them to `dol + dol->section[i]` rather than to `section_addr[i]` directly.
        res = rrc_di_read(
            (void *)((u32)dol + dol->section[i]),
            dol->section_size[i],
            data_header->dol_offset + (dol->section[i] >> 2));
        RRC_ASSERTEQ(res, RRC_DI_LIBDI_OK, "rrc_di_read section");
    }

    rrc_con_update("Initialise DVD: Read Filesystem Table", 50);

    rrc_dbg_printf("mem1 hi: %x, mem2 hi %x\n", mem1_hi, mem2_hi);

    u32 fst_size = data_header->fst_size << 2;
    u32 fst_dest = align_down(mem1_hi - fst_size, 32);
    if (fst_dest < 0x81700000)
    {
        RRC_FATAL("fst size too large");
    }

    mem1_hi = fst_dest;
    rrc_dbg_printf("FST at %x, size: %d, aligned: %d\n", fst_dest, fst_size, align_up(fst_size, 32));
    res = rrc_di_read((void *)fst_dest, align_up(fst_size, 32), data_header->fst_offset);
    RRC_ASSERTEQ(res, RRC_DI_LIBDI_OK, "rrc_di_read fst");

    *((u32 *)0x80000038) = fst_dest; // start of FST

    // read BI2
    mem1_hi = align_down(mem1_hi - RRC_BI2_SIZE, 32);
    void *bi2 = (void *)(mem1_hi);
    res = rrc_di_read(bi2, RRC_BI2_SIZE, 0x440 >> 2);
    RRC_ASSERTEQ(res, RRC_DI_LIBDI_OK, "rrc_di_read for bi2");
    DCStoreRange(bi2, RRC_BI2_SIZE);

    rrc_con_update("Prepare For Patching", 60);

    // start shutting down background threads to boot the game

    WPAD_Shutdown();

    s64 systime_end = gettime();
    rrc_dbg_printf("time taken: %.3f seconds\n", ((f64)diff_msec(systime_start, systime_end)) / 1000.0);

    rrc_loader_load(dol, &stored_settings, bi2, mem1_hi, mem2_hi, region);

    return 0;
}
