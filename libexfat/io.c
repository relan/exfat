/*
	io.c (02.09.09)
	exFAT file system implementation library.

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

#include "exfat.h"
#include <inttypes.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <fcntl.h>
#define __USE_UNIX98 /* for pread() in Linux */
#include <unistd.h>

#if _FILE_OFFSET_BITS != 64
	#error You should define _FILE_OFFSET_BITS=64
#endif

int exfat_open(const char* spec, int ro)
{
	int fd;
	struct stat stbuf;

	fd = open(spec, ro ? O_RDONLY : O_RDWR);
	if (fd < 0)
	{
		exfat_error("failed to open `%s' in read-%s mode", spec,
				ro ? "only" : "write");
		return -1;
	}
	if (fstat(fd, &stbuf) != 0)
	{
		exfat_close(fd);
		exfat_error("failed to fstat `%s'", spec);
		return -1;
	}
	if (!S_ISBLK(stbuf.st_mode) && !S_ISREG(stbuf.st_mode))
	{
		exfat_close(fd);
		exfat_error("`%s' is neither a block device, nor a regular file",
				spec);
		return -1;
	}
	return fd;
}

int exfat_close(int fd)
{
	if (close(fd) != 0)
	{
		exfat_error("close failed");
		return 1;
	}
	return 0;
}

void exfat_pread(int fd, void* buffer, size_t size, off_t offset)
{
	if (pread(fd, buffer, size, offset) != size)
		exfat_bug("failed to read %zu bytes from file at %"PRIu64, size,
				(uint64_t) offset);
}

void exfat_pwrite(int fd, const void* buffer, size_t size, off_t offset)
{
	if (pwrite(fd, buffer, size, offset) != size)
		exfat_bug("failed to write %zu bytes to file at %"PRIu64, size,
				(uint64_t) offset);
}

ssize_t exfat_generic_pread(const struct exfat* ef, struct exfat_node* node,
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
		exfat_pread(ef->fd, bufp, lsize, exfat_c2o(ef, cluster) + loffset);
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	if (!ef->ro && !ef->noatime)
		exfat_update_atime(node);
	return size - remainder;
}

ssize_t exfat_generic_pwrite(struct exfat* ef, struct exfat_node* node,
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
		exfat_pwrite(ef->fd, bufp, lsize, exfat_c2o(ef, cluster) + loffset);
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	exfat_update_mtime(node);
	return size - remainder;
}
