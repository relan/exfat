/*
	cbm.c (09.11.10)
	Clusters Bitmap creation code.

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
#include <limits.h>
#include <inttypes.h>
#include <errno.h>
#include "mkexfat.h"
#include "uct.h"
#include "rootdir.h"

off_t cbm_alignment(void)
{
	return CLUSTER_SIZE(sb);
}

off_t cbm_size(void)
{
	return DIV_ROUND_UP(le32_to_cpu(sb.cluster_count), CHAR_BIT);
}

int cbm_write(off_t base, int fd)
{
	uint32_t allocated_clusters =
			DIV_ROUND_UP(cbm_size(), CLUSTER_SIZE(sb)) +
			DIV_ROUND_UP(uct_size(), CLUSTER_SIZE(sb)) +
			DIV_ROUND_UP(rootdir_size(), CLUSTER_SIZE(sb));
	size_t bitmap_size = DIV_ROUND_UP(allocated_clusters, CHAR_BIT);
	uint8_t* bitmap = malloc(bitmap_size);
	size_t i;

	if (bitmap == NULL)
		return errno;

	for (i = 0; i < bitmap_size * CHAR_BIT; i++)
		if (i < allocated_clusters)
			BMAP_SET(bitmap, i);
		else
			BMAP_CLR(bitmap, i);
	if (write(fd, bitmap, bitmap_size) == -1)
		return errno;
	free(bitmap);

	sb.cluster_block_start = cpu_to_le32(base / BLOCK_SIZE(sb));
	bitmap_entry.start_cluster = cpu_to_le32(OFFSET_TO_CLUSTER(base));
	bitmap_entry.size = cpu_to_le64(cbm_size());
	return 0;
}
