/*
	vbr.c (09.11.10)
	Volume Boot Record creation code.

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
	return 12 * BLOCK_SIZE(sb);
}

static uint32_t vbr_start_checksum(const void* block, size_t size)
{
	size_t i;
	uint32_t sum = 0;

	for (i = 0; i < size; i++)
		/* skip volume_state and allocated_percent fields */
		if (i != 0x6a && i != 0x6b && i != 0x70)
			sum = ((sum << 31) | (sum >> 1)) + ((const uint8_t*) block)[i];
	return sum;
}

static uint32_t vbr_add_checksum(const void* block, size_t size, uint32_t sum)
{
	size_t i;

	for (i = 0; i < size; i++)
		sum = ((sum << 31) | (sum >> 1)) + ((const uint8_t*) block)[i];
	return sum;
}

int vbr_write(off_t base, int fd)
{
	uint32_t checksum;
	le32_t* block = malloc(BLOCK_SIZE(sb));
	size_t i;

	if (block == NULL)
		return errno;

	if (write(fd, &sb, sizeof(struct exfat_super_block)) == -1)
		return errno;
	checksum = vbr_start_checksum(&sb, sizeof(struct exfat_super_block));

	memset(block, 0, BLOCK_SIZE(sb));
	block[BLOCK_SIZE(sb) / sizeof(block[0]) - 1] = cpu_to_le32(0xaa550000);
	for (i = 0; i < 8; i++)
	{
		if (write(fd, block, BLOCK_SIZE(sb)) == -1)
			return errno;
		checksum = vbr_add_checksum(block, BLOCK_SIZE(sb), checksum);
	}

	memset(block, 0, BLOCK_SIZE(sb));
	if (write(fd, block, BLOCK_SIZE(sb)) == -1)
		return errno;
	checksum = vbr_add_checksum(block, BLOCK_SIZE(sb), checksum);
	if (write(fd, block, BLOCK_SIZE(sb)) == -1)
		return errno;
	checksum = vbr_add_checksum(block, BLOCK_SIZE(sb), checksum);

	for (i = 0; i < BLOCK_SIZE(sb) / sizeof(block[0]); i++)
		block[i] = cpu_to_le32(checksum);
	if (write(fd, block, BLOCK_SIZE(sb)) == -1)
		return errno;

	free(block);
	return 0;
}
