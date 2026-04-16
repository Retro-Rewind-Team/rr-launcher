
/*
    crash.c - crash handler when the main game throws an exception

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

#include "crash.h"
#include "console.h"
#include "prompt.h"
#include "util.h"
#include "time.h"
#include "settingsfile.h"

void rrc_crash_handle(void *xfb, struct rrc_settingsfile *settings)
{
    char *lines[] = {
        "- - - Retro Rewind crashed! - - -",
        "---------------------------------",
        "This could have been caused by faulty My Stuff",
        "files, online cheaters, a bug in the pack,",
        "or a corrupted installation.",
    };
    rrc_prompt_1_option(xfb, lines, 5, "Next");

    bool my_stuff_enabled = (settings->my_stuff != RRC_SETTINGSFILE_MY_STUFF_DEFAULT);

    if (my_stuff_enabled)
    {
        char *lines2[] = {
            "- - - Retro Rewind crashed! - - -",
            "---------------------------------",
            "It appears that you have My Stuff enabled.",
            "Before reporting the crash, please try disabling it",
            "and seeing if the crash still occurs."
        };
        rrc_prompt_1_option(xfb, lines2, 5, "OK");
    }
    else
    {
        char *lines2[] = {
            "- - - Retro Rewind crashed! - - -",
            "---------------------------------",
            "If the crash is consistent, try reinstalling",
            "Retro Rewind. Make sure to precisely follow the",
            "instructions found on https://rwfc.net/downloads,",
            "and do not manually delete any files.",
            "",
            "A crash file was written to sd:/RetroRewind6/Crash.pul.",
            "If you continue to experience issues, please report it",
            "along with this file to our Discord server:",
            "https://discord.gg/retrorewind",
        };
        rrc_prompt_1_option(xfb, lines2, 11, "OK");
    }
}

bool rrc_launched_after_crash()
{
    FILE *f = fopen(RRC_CRASH_FILE_PATH, "r");
    if (f)
    {
        fclose(f);
        remove(RRC_CRASH_FILE_PATH);
        return true;
    }
    return false;
}