/*
 *  io.c
 *  exFAT file system implementation library.
 *
 *  Created by Andrew Nayenko on 02.09.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#include "exfat.h"
#include <inttypes.h>
#include <sys/types.h>
#include <sys/uio.h>
#define __USE_UNIX98 /* for pread() in Linux */
#include <unistd.h>

#if _FILE_OFFSET_BITS != 64
	#error You should define _FILE_OFFSET_BITS=64
#endif

void exfat_read_raw(void* buffer, size_t size, off_t offset, int fd)
{
	if (pread(fd, buffer, size, offset) != size)
		exfat_bug("failed to read %zu bytes from file at %"PRIu64, size,
				(uint64_t) offset);
}

void exfat_write_raw(const void* buffer, size_t size, off_t offset, int fd)
{
	if (pwrite(fd, buffer, size, offset) != size)
		exfat_bug("failed to write %zu bytes to file at %"PRIu64, size,
				(uint64_t) offset);
}

ssize_t exfat_read(const struct exfat* ef, struct exfat_node* node,
		void* buffer, size_t size, off_t offset)
{
	cluster_t cluster;
	char* bufp = buffer;
	off_t lsize, loffset, remainder;

	if (offset >= node->size)
		return 0;
	if (size == 0)
		return 0;

	cluster = exfat_advance_cluster(ef, node, offset / CLUSTER_SIZE(*ef->sb));
	if (CLUSTER_INVALID(cluster))
	{
		exfat_error("got invalid cluster");
		return -1;
	}

	loffset = offset % CLUSTER_SIZE(*ef->sb);
	remainder = MIN(size, node->size - offset);
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(cluster))
		{
			exfat_error("got invalid cluster");
			return -1;
		}
		lsize = MIN(CLUSTER_SIZE(*ef->sb) - loffset, remainder);
		exfat_read_raw(bufp, lsize, exfat_c2o(ef, cluster) + loffset, ef->fd);
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	if (!ef->ro && !ef->noatime)
		exfat_update_atime(node);
	return size - remainder;
}

ssize_t exfat_write(struct exfat* ef, struct exfat_node* node,
		const void* buffer, size_t size, off_t offset)
{
	cluster_t cluster;
	const char* bufp = buffer;
	off_t lsize, loffset, remainder;

	if (offset + size > node->size)
	{
		int rc = exfat_truncate(ef, node, offset + size);
		if (rc != 0)
			return rc;
	}
	if (size == 0)
		return 0;

	cluster = exfat_advance_cluster(ef, node, offset / CLUSTER_SIZE(*ef->sb));
	if (CLUSTER_INVALID(cluster))
	{
		exfat_error("got invalid cluster");
		return -1;
	}

	loffset = offset % CLUSTER_SIZE(*ef->sb);
	remainder = size;
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(cluster))
		{
			exfat_error("got invalid cluster");
			return -1;
		}
		lsize = MIN(CLUSTER_SIZE(*ef->sb) - loffset, remainder);
		exfat_write_raw(bufp, lsize, exfat_c2o(ef, cluster) + loffset, ef->fd);
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	exfat_update_mtime(node);
	return size - remainder;
}
