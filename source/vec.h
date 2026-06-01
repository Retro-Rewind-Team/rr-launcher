/*
    vec.h - dynamic array implementation

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

#ifndef RRC_VEC_H
#define RRC_VEC_H

struct vec
{
    void *data;
    int value_size;
    int len;
    int cap;
};

void vec_init(struct vec *vec, int capacity, int value_size);

void *vec_at(struct vec *vec, int index);

void vec_push(struct vec *vec, void *value);

void vec_swap_remove(struct vec *vec, int index);

void vec_free(struct vec *vec);

#endif
