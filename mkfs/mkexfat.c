/*
	mkexfat.c (22.04.12)
	FS creation engine.

	Free exFAT implementation.
	Copyright (C) 2011-2018  Andrew Nayenko

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

#include "mkexfat.h"
#include "vbr.h"
#include "fat.h"
#include "cbm.h"
#include "uct.h"
#include "rootdir.h"
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const struct fs_object* objects[] =
{
	&vbr,
	&vbr,
	&fat,
	/* clusters heap */
	&cbm,
	&uct,
	&rootdir,
	NULL,
};

static struct
{
	int sector_bits;
	int spc_bits;
	off_t volume_size;
	le16_t volume_label[EXFAT_ENAME_MAX + 1];
	uint32_t volume_serial;
	uint64_t first_sector;
}
param;

int get_sector_bits(void)
{
	return param.sector_bits;
}

int get_spc_bits(void)
{
	return param.spc_bits;
}

off_t get_volume_size(void)
{
	return param.volume_size;
}

const le16_t* get_volume_label(void)
{
	return param.volume_label;
}

uint32_t get_volume_serial(void)
{
	return param.volume_serial;
}

uint64_t get_first_sector(void)
{
	return param.first_sector;
}

int get_sector_size(void)
{
	return 1 << get_sector_bits();
}

int get_cluster_size(void)
{
	return get_sector_size() << get_spc_bits();
}

static int setup_spc_bits(int sector_bits, int user_defined, off_t volume_size)
{
	int i;

	if (user_defined != -1)
	{
		off_t cluster_size = 1 << sector_bits << user_defined;
		if (volume_size / cluster_size > EXFAT_LAST_DATA_CLUSTER)
		{
			struct exfat_human_bytes chb, vhb;

			exfat_humanize_bytes(cluster_size, &chb);
			exfat_humanize_bytes(volume_size, &vhb);
			exfat_error("cluster size %"PRIu64" %s is too small for "
					"%"PRIu64" %s volume, try -s %d",
					chb.value, chb.unit,
					vhb.value, vhb.unit,
					1 << setup_spc_bits(sector_bits, -1, volume_size));
			return -1;
		}
		return user_defined;
	}

	if (volume_size < 256LL * 1024 * 1024)
		return MAX(0, 12 - sector_bits);	/* 4 KB */
	if (volume_size < 32LL * 1024 * 1024 * 1024)
		return MAX(0, 15 - sector_bits);	/* 32 KB */

	for (i = 17; ; i++)						/* 128 KB or more */
		if (DIV_ROUND_UP(volume_size, 1 << i) <= EXFAT_LAST_DATA_CLUSTER)
			return MAX(0, i - sector_bits);
}

static int setup_volume_label(le16_t label[EXFAT_ENAME_MAX + 1], const char* s)
{
	memset(label, 0, (EXFAT_ENAME_MAX + 1) * sizeof(le16_t));
	if (s == NULL)
		return 0;
	return exfat_utf8_to_utf16(label, s, EXFAT_ENAME_MAX + 1, strlen(s));
}

static uint32_t setup_volume_serial(uint32_t user_defined)
{
	struct timeval now;

	if (user_defined != 0)
		return user_defined;

	if (gettimeofday(&now, NULL) != 0)
	{
		exfat_error("failed to form volume id");
		return 0;
	}
	return (now.tv_sec << 20) | now.tv_usec;
}

static int check_size(off_t volume_size)
{
	const struct fs_object** pp;
	off_t position = 0;

	for (pp = objects; *pp; pp++)
	{
		position = ROUND_UP(position, (*pp)->get_alignment());
		position += (*pp)->get_size();
	}

	if (position > volume_size)
	{
		struct exfat_human_bytes vhb;

		exfat_humanize_bytes(volume_size, &vhb);
		exfat_error("too small device (%"PRIu64" %s)", vhb.value, vhb.unit);
		return 1;
	}

	return 0;

}

static int erase_object(struct exfat_dev* dev, const void* block,
		off_t block_size, off_t start, off_t size)
{
	const off_t block_count = DIV_ROUND_UP(size, block_size);
	off_t i;

	if (exfat_seek(dev, start, SEEK_SET) == (off_t) -1)
	{
		exfat_error("seek to 0x%"PRIx64" failed", start);
		return 1;
	}
	for (i = 0; i < size; i += block_size)
	{
		if (exfat_write(dev, block, MIN(size - i, block_size)) < 0)
		{
			exfat_error("failed to erase block %"PRIu64"/%"PRIu64
					" at 0x%"PRIx64, i + 1, block_count, start);
			return 1;
		}
	}
	return 0;
}

static int erase(struct exfat_dev* dev)
{
	const struct fs_object** pp;
	off_t position = 0;
	const off_t block_size = 1024 * 1024;
	void* block = malloc(block_size);

	if (block == NULL)
	{
		exfat_error("failed to allocate erase block");
		return 1;
	}
	memset(block, 0, block_size);

	for (pp = objects; *pp; pp++)
	{
		position = ROUND_UP(position, (*pp)->get_alignment());
		if (erase_object(dev, block, block_size, position,
				(*pp)->get_size()) != 0)
		{
			free(block);
			return 1;
		}
		position += (*pp)->get_size();
	}

	free(block);
	return 0;
}

static int create(struct exfat_dev* dev)
{
	const struct fs_object** pp;
	off_t position = 0;

	for (pp = objects; *pp; pp++)
	{
		position = ROUND_UP(position, (*pp)->get_alignment());
		if (exfat_seek(dev, position, SEEK_SET) == (off_t) -1)
		{
			exfat_error("seek to 0x%"PRIx64" failed", position);
			return 1;
		}
		if ((*pp)->write(dev) != 0)
			return 1;
		position += (*pp)->get_size();
	}
	return 0;
}

int exfat_mkfs(struct exfat_dev* dev, int sector_bits, int spc_bits,
		const char* volume_label, uint32_t volume_serial,
		uint64_t first_sector)
{
	param.sector_bits = sector_bits;
	param.first_sector = first_sector;
	param.volume_size = exfat_get_size(dev);

	param.spc_bits = setup_spc_bits(sector_bits, spc_bits, param.volume_size);
	if (param.spc_bits == -1)
		return 1;

	if (setup_volume_label(param.volume_label, volume_label) != 0)
		return 1;

	param.volume_serial = setup_volume_serial(volume_serial);
	if (param.volume_serial == 0)
		return 1;

	if (check_size(param.volume_size) != 0)
		return 1;

	fputs("Creating... ", stdout);
	fflush(stdout);
	if (erase(dev) != 0)
		return 1;
	if (create(dev) != 0)
		return 1;
	puts("done.");

	fputs("Flushing... ", stdout);
	fflush(stdout);
	if (exfat_fsync(dev) != 0)
		return 1;
	puts("done.");

	return 0;
}

off_t get_position(const struct fs_object* object)
{
	const struct fs_object** pp;
	off_t position = 0;

	for (pp = objects; *pp; pp++)
	{
		position = ROUND_UP(position, (*pp)->get_alignment());
		if (*pp == object)
			return position;
		position += (*pp)->get_size();
	}
	exfat_bug("unknown object");
}
