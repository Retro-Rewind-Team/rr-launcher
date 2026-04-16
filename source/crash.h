/*
    crash.h - crash handler when the main game throws an exception

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

#ifndef RRC_CRASH_H
#define RRC_CRASH_H

#include "settingsfile.h"

#define RRC_CRASH_FILE_PATH "/RetroRewindChannel/.crash"

/* 
    Run the post-crash handler.
    Prompts the user about the crash and asks them if they want to upload
    RetroRewind6/Crash.pul to the servers (if any such file exists).
    Then clears the flag and returns.
*/
void rrc_crash_handle(void *xfb, struct rrc_settingsfile *settings);

/*
    Checks for the necessary ephemeral file that indicates whether we were launched after a crash.
    If it exists, deletes it and returns true. Otherwise, returns false.
*/
bool rrc_launched_after_crash();

#endif