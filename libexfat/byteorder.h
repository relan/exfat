/*
	byteorder.h (12.01.10)
	Endianness stuff. exFAT uses little-endian byte order.

	Free exFAT implementation.
	Copyright (C) 2010-2023  Andrew Nayenko

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef BYTEORDER_H_INCLUDED
#define BYTEORDER_H_INCLUDED

#include "config.h"
#include <stdint.h>
#include <stddef.h>

#ifdef WORDS_BIGENDIAN
typedef unsigned char bitmap_t;
#else
typedef size_t bitmap_t;
#endif

typedef struct { uint16_t __u16; } le16_t;
typedef struct { uint32_t __u32; } le32_t;
typedef struct { uint64_t __u64; } le64_t;

static inline uint16_t le16_to_cpu(le16_t v) {
	unsigned char *p = (unsigned char *)&v;
	return p[0] | p[1] << 8;
}

static inline uint32_t le32_to_cpu(le32_t v) {
	unsigned char *p = (unsigned char *)&v;
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint64_t le64_to_cpu(le64_t v) {
	unsigned char *p = (unsigned char *)&v;
	uint64_t rval = 0;

	rval |= (uint64_t)p[0]       | (uint64_t)p[1] <<  8;
	rval |= (uint64_t)p[2] << 16 | (uint64_t)p[3] << 24;
	rval |= (uint64_t)p[4] << 32 | (uint64_t)p[5] << 40;
	rval |= (uint64_t)p[6] << 48 | (uint64_t)p[7] << 56;
	return rval;
}

static inline le16_t cpu_to_le16(uint16_t v) {
	le16_t t;
	unsigned char *p = (unsigned char *)&t;
	p[0] = v      ; p[1] = v >>  8;
	return t;
}
static inline le32_t cpu_to_le32(uint32_t v) {
	le32_t t;
	unsigned char *p = (unsigned char *)&t;
	p[0] = v      ; p[1] = v >>= 8; p[2] = v >>= 8; p[3] = v >>  8;
	return t;
}

static inline le64_t cpu_to_le64(uint64_t v) {
	le64_t t;
	unsigned char *p = (unsigned char *)&t;
	p[0] = v      ; p[1] = v >>= 8; p[2] = v >>= 8; p[3] = v >>= 8;
	p[4] = v >>= 8; p[5] = v >>= 8; p[6] = v >>= 8; p[7] = v >>  8;
	return t;
}

#endif /* ifndef BYTEORDER_H_INCLUDED */
