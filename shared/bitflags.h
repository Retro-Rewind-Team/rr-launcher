/*
    bitflags.h - definition of bit flags

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

#ifndef SHARED_BITFLAGS_H
#define SHARED_BITFLAGS_H

/*
    These bitflags are a one-way street - the channel can pass them to the game,
    but the game cannot set anything to pass back to the channel.

    This is because the routine that loads the channel causes all unused memory to be zeroed.
    Therefore, for things like crashed or loaded from RR, we cannot use these flags
    and instead must pass these values around as ephemeral files that the channel checks for
    and then deletes once read.
*/

#define RRC_BITFLAGS_SAVEGAME (1 << 0)
// 1 << 1 and 1 << 2 currently unused

#define RRC_BITFLAGS_MY_STUFF_CTGP (1 << 3)
#define RRC_BITFLAGS_MY_STUFF_RR (1 << 4)
#define RRC_BITFLAGS_MY_STUFF_CTGP_MUSIC (1 << 5)
#define RRC_BITFLAGS_MY_STUFF_RR_MUSIC (1 << 6)
#define RRC_BITFLAGS_MY_STUFF_ANY (RRC_BITFLAGS_MY_STUFF_CTGP | RRC_BITFLAGS_MY_STUFF_RR | RRC_BITFLAGS_MY_STUFF_CTGP_MUSIC | RRC_BITFLAGS_MY_STUFF_RR_MUSIC)
#define RRC_BITFLAGS_MY_STUFF_ANY_MUSIC (RRC_BITFLAGS_MY_STUFF_CTGP_MUSIC | RRC_BITFLAGS_MY_STUFF_RR_MUSIC)

#endif
