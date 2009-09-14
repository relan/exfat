/*
 *  cluster.c
 *  exFAT file system implementation library.
 *
 *  Created by Andrew Nayenko on 03.09.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#include "exfat.h"

/*
 * Cluster to block.
 */
static uint32_t c2b(const struct exfat* ef, cluster_t cluster)
{
	if (cluster < EXFAT_FIRST_DATA_CLUSTER)
		exfat_bug("invalid cluster number %u", cluster);
	return le32_to_cpu(ef->sb->cluster_block_start) +
		((cluster - EXFAT_FIRST_DATA_CLUSTER) << ef->sb->bpc_bits);
}

/*
 * Cluster to absolute offset.
 */
off_t exfat_c2o(const struct exfat* ef, cluster_t cluster)
{
	return (off_t) c2b(ef, cluster) << ef->sb->block_bits;
}

/*
 * Block to absolute offset.
 */
static off_t b2o(const struct exfat* ef, uint32_t block)
{
	return (off_t) block << ef->sb->block_bits;
}

cluster_t exfat_next_cluster(const struct exfat* ef, cluster_t cluster,
		int contiguous)
{
	cluster_t next;
	off_t fat_offset;

	if (contiguous)
		return cluster + 1;
	fat_offset = b2o(ef, le32_to_cpu(ef->sb->fat_block_start))
		+ cluster * sizeof(cluster_t);
	exfat_read_raw(&next, sizeof(next), fat_offset, ef->fd);
	return next;
}

cluster_t exfat_advance_cluster(const struct exfat* ef, cluster_t cluster,
		int contiguous, uint32_t count)
{
	uint32_t i;

	for (i = 0; i < count; i++)
	{
		cluster = exfat_next_cluster(ef, cluster, contiguous);
		if (CLUSTER_INVALID(cluster))
			break;
	}
	return cluster;
}
