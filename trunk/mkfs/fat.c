/*
	fat.c (09.11.10)
	File Allocation Table creation code.

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
#include <inttypes.h>
#include <errno.h>
#include "mkexfat.h"
#include "cbm.h"
#include "uct.h"
#include "rootdir.h"

off_t fat_alignment(void)
{
	return (off_t) le32_to_cpu(sb.fat_block_start) * BLOCK_SIZE(sb);
}

off_t fat_size(void)
{
	return (off_t) le32_to_cpu(sb.cluster_count) * sizeof(cluster_t);
}

static cluster_t fat_write_entry(cluster_t cluster, cluster_t value, int fd)
{
	le32_t fat_entry = cpu_to_le32(value);
	if (write(fd, &fat_entry, sizeof(fat_entry)) == -1)
		return 0;
	return cluster + 1;
}

static cluster_t fat_write_entries(cluster_t cluster, uint64_t length, int fd)
{
	cluster_t end = cluster + DIV_ROUND_UP(length, CLUSTER_SIZE(sb));

	while (cluster < end - 1)
	{
		cluster = fat_write_entry(cluster, cluster + 1, fd);
		if (cluster == 0)
			return 0;
	}
	return fat_write_entry(cluster, EXFAT_CLUSTER_END, fd);
}

int fat_write(off_t base, int fd)
{
	cluster_t c = 0;

	if (base != le32_to_cpu(sb.fat_block_start) * BLOCK_SIZE(sb))
		exfat_bug("unexpected FAT location: %"PRIu64" (expected %u)",
				base, le32_to_cpu(sb.fat_block_start) * BLOCK_SIZE(sb));

	if (!(c = fat_write_entry(c, 0xfffffff8, fd))) /* media type */
		return errno;
	if (!(c = fat_write_entry(c, 0xffffffff, fd))) /* some weird constant */
		return errno;
	if (!(c = fat_write_entries(c, cbm_size(), fd)))
		return errno;
	if (!(c = fat_write_entries(c, uct_size(), fd)))
		return errno;
	if (!(c = fat_write_entries(c, rootdir_size(), fd)))
		return errno;

	return 0;
}
