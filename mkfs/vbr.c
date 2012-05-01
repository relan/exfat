/*
	vbr.c (09.11.10)
	Volume Boot Record creation code.

	Copyright (C) 2011, 2012  Andrew Nayenko

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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "mkexfat.h"

off_t vbr_alignment(void)
{
	return 1;
}

off_t vbr_size(void)
{
	return 12 * SECTOR_SIZE(sb);
}

int vbr_write(struct exfat_dev* dev, off_t base)
{
	uint32_t checksum;
	le32_t* sector = malloc(SECTOR_SIZE(sb));
	size_t i;

	if (sector == NULL)
		return errno;

	if (exfat_write(dev, &sb, sizeof(struct exfat_super_block)) < 0)
	{
		free(sector);
		return errno;
	}
	checksum = exfat_vbr_start_checksum(&sb, sizeof(struct exfat_super_block));

	memset(sector, 0, SECTOR_SIZE(sb));
	sector[SECTOR_SIZE(sb) / sizeof(sector[0]) - 1] = cpu_to_le32(0xaa550000);
	for (i = 0; i < 8; i++)
	{
		if (exfat_write(dev, sector, SECTOR_SIZE(sb)) < 0)
		{
			free(sector);
			return errno;
		}
		checksum = exfat_vbr_add_checksum(sector, SECTOR_SIZE(sb), checksum);
	}

	memset(sector, 0, SECTOR_SIZE(sb));
	if (exfat_write(dev, sector, SECTOR_SIZE(sb)) < 0)
	{
		free(sector);
		return errno;
	}
	checksum = exfat_vbr_add_checksum(sector, SECTOR_SIZE(sb), checksum);
	if (exfat_write(dev, sector, SECTOR_SIZE(sb)) < 0)
	{
		free(sector);
		return errno;
	}
	checksum = exfat_vbr_add_checksum(sector, SECTOR_SIZE(sb), checksum);

	for (i = 0; i < SECTOR_SIZE(sb) / sizeof(sector[0]); i++)
		sector[i] = cpu_to_le32(checksum);
	if (exfat_write(dev, sector, SECTOR_SIZE(sb)) < 0)
	{
		free(sector);
		return errno;
	}

	free(sector);
	return 0;
}
