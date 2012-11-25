/*
	io.c (02.09.09)
	exFAT file system implementation library.

	Copyright (C) 2010-2012  Andrew Nayenko

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
#include <unistd.h>
#include <string.h>
#ifdef USE_UBLIO
#include <sys/uio.h>
#include <ublio.h>
#endif

#if !defined(_FILE_OFFSET_BITS) || (_FILE_OFFSET_BITS != 64)
	#error You should define _FILE_OFFSET_BITS=64
#endif

struct exfat_dev
{
	int fd;
#ifdef USE_UBLIO
	off_t pos;
	ublio_filehandle_t ufh;
#endif
};

struct exfat_dev* exfat_open(const char* spec, int ro)
{
	struct exfat_dev* dev;
	struct stat stbuf;
#ifdef USE_UBLIO
	struct ublio_param up;
#endif

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
	if (!S_ISBLK(stbuf.st_mode) &&
		!S_ISCHR(stbuf.st_mode) &&
		!S_ISREG(stbuf.st_mode))
	{
		close(dev->fd);
		free(dev);
		exfat_error("`%s' is neither a device, nor a regular file", spec);
		return NULL;
	}

#ifdef USE_UBLIO
	memset(&up, 0, sizeof(struct ublio_param));
	up.up_blocksize = 256 * 1024;
	up.up_items = 64;
	up.up_grace = 32;
	up.up_priv = &dev->fd;

	dev->pos = 0;
	dev->ufh = ublio_open(&up);
	if (dev->ufh == NULL)
	{
		close(dev->fd);
		free(dev);
		exfat_error("failed to initialize ublio");
		return NULL;
	}
#endif

	return dev;
}

int exfat_close(struct exfat_dev* dev)
{
#ifdef USE_UBLIO
	if (ublio_close(dev->ufh) != 0)
		exfat_error("failed to close ublio");
#endif
	if (close(dev->fd) != 0)
	{
		free(dev);
		exfat_error("failed to close device");
		return 1;
	}
	free(dev);
	return 0;
}

int exfat_fsync(struct exfat_dev* dev)
{
#ifdef USE_UBLIO
	if (ublio_fsync(dev->ufh) != 0)
#else
	if (fsync(dev->fd) != 0)
#endif
	{
		exfat_error("fsync failed");
		return 1;
	}
	return 0;
}

off_t exfat_seek(struct exfat_dev* dev, off_t offset, int whence)
{
#ifdef USE_UBLIO
	/* XXX SEEK_CUR will be handled incorrectly */
	return dev->pos = lseek(dev->fd, offset, whence);
#else
	return lseek(dev->fd, offset, whence);
#endif
}

ssize_t exfat_read(struct exfat_dev* dev, void* buffer, size_t size)
{
#ifdef USE_UBLIO
	ssize_t result = ublio_pread(dev->ufh, buffer, size, dev->pos);
	if (result >= 0)
		dev->pos += size;
	return result;
#else
	return read(dev->fd, buffer, size);
#endif
}

ssize_t exfat_write(struct exfat_dev* dev, const void* buffer, size_t size)
{
#ifdef USE_UBLIO
	ssize_t result = ublio_pwrite(dev->ufh, buffer, size, dev->pos);
	if (result >= 0)
		dev->pos += size;
	return result;
#else
	return write(dev->fd, buffer, size);
#endif
}

void exfat_pread(struct exfat_dev* dev, void* buffer, size_t size,
		off_t offset)
{
#ifdef USE_UBLIO
	if (ublio_pread(dev->ufh, buffer, size, offset) != size)
#else
	if (pread(dev->fd, buffer, size, offset) != size)
#endif
		exfat_bug("failed to read %zu bytes from file at %"PRIu64, size,
				(uint64_t) offset);
}

void exfat_pwrite(struct exfat_dev* dev, const void* buffer, size_t size,
		off_t offset)
{
#ifdef USE_UBLIO
	if (ublio_pwrite(dev->ufh, buffer, size, offset) != size)
#else
	if (pwrite(dev->fd, buffer, size, offset) != size)
#endif
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
		exfat_error("invalid cluster 0x%x while reading", cluster);
		return -1;
	}

	loffset = offset % CLUSTER_SIZE(*ef->sb);
	remainder = MIN(size, node->size - offset);
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(cluster))
		{
			exfat_error("invalid cluster 0x%x while reading", cluster);
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
		exfat_error("invalid cluster 0x%x while writing", cluster);
		return -1;
	}

	loffset = offset % CLUSTER_SIZE(*ef->sb);
	remainder = size;
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(cluster))
		{
			exfat_error("invalid cluster 0x%x while writing", cluster);
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
