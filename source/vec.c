/*
    vec.c - dynamic array implementation

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

#include "vec.h"
#include "util.h"
#include <string.h>

#define _RRC_VEC_BOUNDS_CHECK(vec, index) \
    RRC_ASSERT(index >= 0 && index < vec->len, "vec index out of bounds")

void vec_init(struct vec *vec, int capacity, int value_size)
{
    vec->data = malloc(value_size * capacity);
    RRC_ASSERT(vec->data != NULL, "OOM while allocating vec data");
    vec->value_size = value_size;
    vec->len = 0;
    vec->cap = capacity;
}

void *vec_at(struct vec *vec, int index)
{
    _RRC_VEC_BOUNDS_CHECK(vec, index);
    return (char *)vec->data + (index * vec->value_size);
}

void vec_push(struct vec *vec, void *value)
{
    if (vec->len >= vec->cap)
    {
        RRC_ASSERT(vec->cap < 0x3FFFFFFF, "vec capacity too large"); // Prevent overflow when doubling capacity.
        int new_cap = vec->cap * 2;

        void *new_data = realloc(vec->data, new_cap * vec->value_size);
        RRC_ASSERT(new_data != NULL, "OOM while reallocating vec data");
        vec->data = new_data;
        vec->cap = new_cap;
    }

    vec->len++;
    void *dest = vec_at(vec, vec->len - 1);
    memcpy(dest, value, vec->value_size);
}

void vec_swap_remove(struct vec *vec, int index)
{
    _RRC_VEC_BOUNDS_CHECK(vec, index);
    if (index != vec->len - 1)
    {
        void *last_value = vec_at(vec, vec->len - 1);
        void *dest = vec_at(vec, index);
        memcpy(dest, last_value, vec->value_size);
    }
    vec->len--;
}

void vec_free(struct vec *vec)
{
    free(vec->data);
}
