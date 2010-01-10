/*
	exfatfs.h (29.08.09)
	Definitions of structures and constants used in exFAT file system.

	Copyright (C) 2009, 2010  Andrew Nayenko

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef EXFATFS_H_INCLUDED
#define EXFATFS_H_INCLUDED

#include <stdint.h>
#include <endian.h>
#include <byteswap.h>

typedef uint32_t cluster_t;		/* cluster number */

typedef struct { uint16_t __u16; } le16_t;
typedef struct { uint32_t __u32; } le32_t;
typedef struct { uint64_t __u64; } le64_t;

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint16_t le16_to_cpu(le16_t v) { return v.__u16; }
static inline uint32_t le32_to_cpu(le32_t v) { return v.__u32; }
static inline uint64_t le64_to_cpu(le64_t v) { return v.__u64; }

static inline le16_t cpu_to_le16(uint16_t v) { le16_t t = {v}; return t; }
static inline le32_t cpu_to_le32(uint32_t v) { le32_t t = {v}; return t; }
static inline le64_t cpu_to_le64(uint64_t v) { le64_t t = {v}; return t; }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint16_t le16_to_cpu(le16_t v) { return bswap_16(v.__u16); }
static inline uint32_t le32_to_cpu(le32_t v) { return bswap_32(v.__u32); }
static inline uint64_t le64_to_cpu(le64_t v) { return bswap_64(v.__u64); }

static inline le16_t cpu_to_le16(uint16_t v)
	{ le16_t t = {bswap_16(v)}; return t; }
static inline le32_t cpu_to_le32(uint32_t v)
	{ le32_t t = {bswap_32(v)}; return t; }
static inline le64_t cpu_to_le64(uint64_t v)
	{ le64_t t = {bswap_64(v)}; return t; }
#else
#error Wow! You have a PDP machine?!
#endif

#define EXFAT_FIRST_DATA_CLUSTER 2

#define EXFAT_CLUSTER_FREE         0 /* free cluster */
#define EXFAT_CLUSTER_BAD 0xfffffff7 /* cluster contains bad block */
#define EXFAT_CLUSTER_END 0xffffffff /* final cluster of file or directory */

struct exfat_super_block
{
	uint8_t jump[3];				/* 0x00 jmp and nop instructions */
	uint8_t oem_name[8];			/* 0x03 "EXFAT   " */
	uint8_t	__unknown1[53];			/* 0x0B ? always 0 */
	le64_t block_start;				/* 0x40 partition first block */
	le64_t block_count;				/* 0x48 partition blocks count */
	le32_t fat_block_start;			/* 0x50 FAT first block */
	le32_t fat_block_count;			/* 0x54 FAT blocks count */
	le32_t cluster_block_start;		/* 0x58 first cluster block */
	le32_t cluster_count;			/* 0x5C total clusters count */
	le32_t rootdir_cluster;			/* 0x60 first cluster of the root dir */
	le32_t volume_serial;			/* 0x64 */
	le16_t __unknown2;				/* 0x68 version? always 0x00 0x01 */
	le16_t volume_state;			/* 0x6A */
	uint8_t block_bits;				/* 0x6C block size as (1 << n) */
	uint8_t bpc_bits;				/* 0x6D blocks per cluster as (1 << n) */
	uint8_t __unknown3;				/* 0x6E ? always 1 */
	uint8_t __unknown4;				/* 0x6F drive number? always 0x80 */
	uint8_t allocated_percent;		/* 0x70 percentage of allocated space */
	uint8_t __unknown5[397];		/* 0x71 padding? all zero */
	le16_t boot_signature;			/* the value of 0xAA55 */
};

#define EXFAT_ENTRY_VALID     0x80
#define EXFAT_ENTRY_CONTINUED 0x40

#define EXFAT_ENTRY_EOD       (0x00)
#define EXFAT_ENTRY_BITMAP    (0x01 | EXFAT_ENTRY_VALID)
#define EXFAT_ENTRY_UPCASE    (0x02 | EXFAT_ENTRY_VALID)
#define EXFAT_ENTRY_LABEL     (0x03 | EXFAT_ENTRY_VALID)
#define EXFAT_ENTRY_FILE      (0x05 | EXFAT_ENTRY_VALID)
#define EXFAT_ENTRY_FILE_INFO (0x00 | EXFAT_ENTRY_VALID | EXFAT_ENTRY_CONTINUED)
#define EXFAT_ENTRY_FILE_NAME (0x01 | EXFAT_ENTRY_VALID | EXFAT_ENTRY_CONTINUED)

struct exfat_entry					/* common container for all entries */
{
	uint8_t type;					/* any of EXFAT_ENTRY_xxx */
	uint8_t data[31];
};

#define EXFAT_ENAME_MAX 15

struct exfat_bitmap					/* allocated clusters bitmap */
{
	uint8_t type;					/* EXFAT_ENTRY_BITMAP */
	uint8_t __unknown1[19];
	le32_t start_cluster;
	le64_t size;					/* in bytes */
};

struct exfat_upcase					/* upper case translation table */
{
	uint8_t type;					/* EXFAT_ENTRY_UPCASE */
	uint8_t __unknown1[3];
	le32_t checksum;
	uint8_t __unknown2[12];
	le32_t start_cluster;
	le64_t size;					/* in bytes */
};

struct exfat_label					/* volume label */
{
	uint8_t type;					/* EXFAT_ENTRY_LABEL */
	uint8_t length;					/* number of characters */
	le16_t name[EXFAT_ENAME_MAX];	/* in UTF-16LE */
};

#define EXFAT_ATTRIB_RO     0x01
#define EXFAT_ATTRIB_HIDDEN 0x02
#define EXFAT_ATTRIB_SYSTEM 0x04
#define EXFAT_ATTRIB_VOLUME 0x08
#define EXFAT_ATTRIB_DIR    0x10
#define EXFAT_ATTRIB_ARCH   0x20

struct exfat_file					/* file or directory */
{
	uint8_t type;					/* EXFAT_ENTRY_FILE */
	uint8_t continuations;
	le16_t checksum;
	le16_t attrib;					/* combination of EXFAT_ATTRIB_xxx */
	le16_t __unknown1;
	le16_t crtime, crdate;			/* creation date and time */
	le16_t mtime, mdate;			/* latest modification date and time */
	le16_t atime, adate;			/* latest access date and time */
	uint8_t crtime_cs;				/* creation time in cs (centiseconds) */
	uint8_t mtime_cs;				/* latest modification time in cs */
	uint8_t __unknown2[10];
};

#define EXFAT_FLAG_FRAGMENTED 1
#define EXFAT_FLAG_CONTIGUOUS 3

struct exfat_file_info				/* file or directory info */
{
	uint8_t type;					/* EXFAT_ENTRY_FILE_INFO */
	uint8_t flag;					/* fragmented or contiguous */
	uint8_t __unknown1;
	uint8_t name_length;
	le16_t name_hash;
	le16_t __unknown2;
	le64_t real_size;				/* in bytes, equals to size */
	uint8_t __unknown3[4];
	le32_t start_cluster;
	le64_t size;					/* in bytes, equals to real_size */
};

struct exfat_file_name				/* file or directory name */
{
	uint8_t type;					/* EXFAT_ENTRY_FILE_NAME */
	uint8_t __unknown;
	le16_t name[EXFAT_ENAME_MAX];	/* in UTF-16LE */
};

#endif /* ifndef EXFATFS_H_INCLUDED */
