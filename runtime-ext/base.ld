/*
    base.ld - Base linker script independent of regions

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

PHDRS {
    # define a separate program header for the relevant symbols we want to expose at fixed addresses,
    # otherwise ld merges them together with bss which confuses elf2dol
    dvd PT_LOAD;
}

SECTIONS {
    # .init, .text and .rodata are specified in Makefile LDFLAGS
    .dvd_trampolines              0x81782d60 : { *(.dvd_trampolines) }              :dvd
    .sd_vtable                    0x81782e00 : { *(.sd_vtable) }                    :dvd
    .dvd_convert_path_to_entrynum 0x81782e60 : { *(.dvd_convert_path_to_entrynum) } :dvd
    .dvd_open                     0x81782ea0 : { *(.dvd_open) }                     :dvd
    .dvd_fast_open                0x81782ee0 : { *(.dvd_fast_open) }                :dvd
    .dvd_read_prio                0x81782f20 : { *(.dvd_read_prio) }                :dvd
    .dvd_close                    0x81782f60 : { *(.dvd_close) }                    :dvd
    .riivo_disc                   0x81782fa0 : { *(.riivo_disc) }                   :dvd

    PROVIDE(dvd_convert_path_to_entrynum_trampoline = ADDR(.dvd_trampolines));
    PROVIDE(dvd_fast_open_trampoline = ADDR(.dvd_trampolines) + 32);
    PROVIDE(dvd_open_trampoline = ADDR(.dvd_trampolines) + 64);
    PROVIDE(dvd_read_prio_trampoline = ADDR(.dvd_trampolines) + 96);

    PROVIDE(rrc_bitflags = 0x817ffff0);

    # bss is placed at the very end, so we make sure the DOL never grows too big to overflow mem1.
    # We also never write to the last 64 bytes of mem1 either, that's reserved for some other static data
    # like the bitflags and signature
    ASSERT(ADDR(.bss) + SIZEOF(.bss) <= (0x817FFFFF - 64), "bss section overflows mem1")
}
