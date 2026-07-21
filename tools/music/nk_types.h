#ifndef NK_TYPES_H
#define NK_TYPES_H

#include <stddef.h>

typedef unsigned char NkU8;
typedef signed char NkS8;
typedef unsigned short NkU16;
typedef signed short NkS16;
typedef unsigned int NkU32;
typedef signed int NkS32;

typedef char nk_u8_must_be_1[(sizeof(NkU8) == 1) ? 1 : -1];
typedef char nk_u16_must_be_2[(sizeof(NkU16) == 2) ? 1 : -1];
typedef char nk_u32_must_be_4[(sizeof(NkU32) == 4) ? 1 : -1];

#define NK_ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define NK_UNUSED(x) ((void)(x))

#endif
