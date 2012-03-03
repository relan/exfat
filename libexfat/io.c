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

#define _XOPEN_SOURCE 500 /* for pread() and pwrite() in Linux */
#include "exfat.h"
#include <inttypes.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if _FILE_OFFSET_BITS != 64
	#error You should define _FILE_OFFSET_BITS=64
#endif

struct exfat_dev
{
	int fd;
};

struct exfat_dev* exfat_open(const char* spec, int ro)
{
	struct exfat_dev* dev;
	struct stat stbuf;

	dev = malloc(sizeof(struct exfat_dev));
	if (dev == NULL)
	{
		exfat_error("failed to allocate memory for device structure");
		return NULL;
	}

	dev->fd = open(spec, ro ? O_RDONLY : O_RDWR);
	if (dev->fd < 0)
	{
		free(dev);
		exfat_error("failed to open `%s' in read-%s mode", spec,
				ro ? "only" : "write");
		return NULL;
	}
	if (fstat(dev->fd, &stbuf) != 0)
	{
		close(dev->fd);
		free(dev);
		exfat_error("failed to fstat `%s'", spec);
		return NULL;
	}
	if (!S_ISBLK(stbuf.st_mode) && !S_ISREG(stbuf.st_mode))
	{
		close(dev->fd);
		free(dev);
		exfat_error("`%s' is neither a block device, nor a regular file",
				spec);
		return NULL;
	}
	return dev;
}

int exfat_close(struct exfat_dev* dev)
{
	if (close(dev->fd) != 0)
	{
		free(dev);
		exfat_error("close failed");
		return 1;
	}
	free(dev);
	return 0;
}

int exfat_fsync(struct exfat_dev* dev)
{
	if (fsync(dev->fd) != 0)
	{
		exfat_error("fsync failed");
		return 1;
	}
	return 0;
}

off_t exfat_seek(struct exfat_dev* dev, off_t offset, int whence)
{
	return lseek(dev->fd, offset, whence);
}

ssize_t exfat_read(struct exfat_dev* dev, void* buffer, size_t size)
{
	return read(dev->fd, buffer, size);
}

ssize_t exfat_write(struct exfat_dev* dev, const void* buffer, size_t size)
{
	return write(dev->fd, buffer, size);
}

void exfat_pread(struct exfat_dev* dev, void* buffer, size_t size,
		off_t offset)
{
	if (pread(dev->fd, buffer, size, offset) != size)
		exfat_bug("failed to read %zu bytes from file at %"PRIu64, size,
				(uint64_t) offset);
}

void exfat_pwrite(struct exfat_dev* dev, const void* buffer, size_t size,
		off_t offset)
{
	if (pwrite(dev->fd, buffer, size, offset) != size)
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
		exfat_pread(ef->dev, bufp, lsize, exfat_c2o(ef, cluster) + loffset);
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
		exfat_pwrite(ef->dev, bufp, lsize, exfat_c2o(ef, cluster) + loffset);
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	exfat_update_mtime(node);
	return size - remainder;
}
