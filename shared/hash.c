#include "hash.h"
#include <ctype.h>

#define _RTE_HASH_STRING_MULTIPLIER 31

u32 rrc_hash_string(const char *str)
{
    u32 hash = 0;
    while (*str)
    {
        hash = hash * _RTE_HASH_STRING_MULTIPLIER + (unsigned char)(*str);
        str++;
    }
    return hash;
}

u32 rrc_hash_string_lowercase(const char *str)
{
    u32 hash = 0;
    while (*str)
    {
        hash = hash * _RTE_HASH_STRING_MULTIPLIER + (unsigned char)tolower(*str);
        str++;
    }
    return hash;
}
