/*
	io.c (02.09.09)
	exFAT file system implementation library.

	Free exFAT implementation.
	Copyright (C) 2010-2018  Andrew Nayenko

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

#include "exfat.h"
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#if defined(__APPLE__)
#include <sys/disk.h>
#elif defined(__OpenBSD__)
#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#endif
#include <sys/ioctl.h>
#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMARCROS
#include <sys/sysmacros.h>
#endif

struct exfat_dev
{
	int fd;
	enum exfat_mode mode;
	off_t block_discard_alignment;
	off_t block_discard_granularity;
	off_t block_discard_max_bytes;
	off_t size; /* in bytes */
};

static bool is_open(int fd)
{
	return fcntl(fd, F_GETFD) != -1;
}

static int open_ro(const char* spec)
{
	return open(spec, O_RDONLY);
}

static int open_rw(const char* spec)
{
	int fd = open(spec, O_RDWR);
#ifdef __linux__
	int ro = 0;

	/*
	   This ioctl is needed because after "blockdev --setro" kernel still
	   allows to open the device in read-write mode but fails writes.
	*/
	if (fd != -1 && ioctl(fd, BLKROGET, &ro) == 0 && ro)
	{
		close(fd);
		errno = EROFS;
		return -1;
	}
#endif
	return fd;
}

__attribute__((format(scanf, 4, 5)))
static int read_sys_block_attr(int dev_major, int dev_minor, const char* attr,
		const char* format, ...)
{
	char path[128];
	FILE *fp;
	va_list args;
	int rc;

	snprintf(path, sizeof(path), "/sys/dev/block/%d:%d/%s", dev_major, dev_minor, attr);
	fp = fopen(path, "r");
	if (!fp)
	{
		snprintf(path, sizeof(path), "/sys/dev/block/%d:%d/../%s", dev_major, dev_minor, attr);
		fp = fopen(path, "r");
	}

	if (!fp)
		return -ENOENT;

	va_start(args, format);
	rc = vfscanf(fp, format, args);
	va_end(args);

	fclose(fp);
	return rc;
}

struct exfat_dev* exfat_open(const char* spec, enum exfat_mode mode)
{
	struct exfat_dev* dev;
	struct stat stbuf;

	/* The system allocates file descriptors sequentially. If we have been
	   started with stdin (0), stdout (1) or stderr (2) closed, the system
	   will give us descriptor 0, 1 or 2 later when we open block device,
	   FUSE communication pipe, etc. As a result, functions using stdin,
	   stdout or stderr will actually work with a different thing and can
	   corrupt it. Protect descriptors 0, 1 and 2 from such misuse. */
	while (!is_open(STDIN_FILENO)
		|| !is_open(STDOUT_FILENO)
		|| !is_open(STDERR_FILENO))
	{
		/* we don't need those descriptors, let them leak */
		if (open("/dev/null", O_RDWR) == -1)
		{
			exfat_error("failed to open /dev/null");
			return NULL;
		}
	}

	dev = calloc(1, sizeof(struct exfat_dev));
	if (dev == NULL)
	{
		exfat_error("failed to allocate memory for device structure");
		return NULL;
	}

	switch (mode)
	{
	case EXFAT_MODE_RO:
		dev->fd = open_ro(spec);
		if (dev->fd == -1)
		{
			free(dev);
			exfat_error("failed to open '%s' in read-only mode: %s", spec,
					strerror(errno));
			return NULL;
		}
		dev->mode = EXFAT_MODE_RO;
		break;
	case EXFAT_MODE_RW:
		dev->fd = open_rw(spec);
		if (dev->fd == -1)
		{
			free(dev);
			exfat_error("failed to open '%s' in read-write mode: %s", spec,
					strerror(errno));
			return NULL;
		}
		dev->mode = EXFAT_MODE_RW;
		break;
	case EXFAT_MODE_ANY:
		dev->fd = open_rw(spec);
		if (dev->fd != -1)
		{
			dev->mode = EXFAT_MODE_RW;
			break;
		}
		dev->fd = open_ro(spec);
		if (dev->fd != -1)
		{
			dev->mode = EXFAT_MODE_RO;
			exfat_warn("'%s' is write-protected, mounting read-only", spec);
			break;
		}
		free(dev);
		exfat_error("failed to open '%s': %s", spec, strerror(errno));
		return NULL;
	}

	if (fstat(dev->fd, &stbuf) != 0)
	{
		close(dev->fd);
		free(dev);
		exfat_error("failed to fstat '%s'", spec);
		return NULL;
	}
	if (!S_ISBLK(stbuf.st_mode) &&
		!S_ISCHR(stbuf.st_mode) &&
		!S_ISREG(stbuf.st_mode))
	{
		close(dev->fd);
		free(dev);
		exfat_error("'%s' is neither a device, nor a regular file", spec);
		return NULL;
	}
#ifdef __linux__
	if (S_ISBLK(stbuf.st_mode))
	{
		uintmax_t discard_alignment = 0;
		uintmax_t discard_granularity = 0;
		uintmax_t discard_max_bytes = 0;
		read_sys_block_attr(major(stbuf.st_rdev), minor(stbuf.st_rdev),
				"discard_alignment", "%"SCNuMAX, &discard_alignment);
		read_sys_block_attr(major(stbuf.st_rdev), minor(stbuf.st_rdev),
				"queue/discard_granularity", "%"SCNuMAX, &discard_granularity);
		read_sys_block_attr(major(stbuf.st_rdev), minor(stbuf.st_rdev),
				"queue/discard_max_bytes", "%"SCNuMAX, &discard_max_bytes);
		dev->block_discard_alignment = discard_alignment;
		dev->block_discard_granularity = discard_granularity;
		dev->block_discard_max_bytes = discard_max_bytes;
		if (discard_granularity > 0 && discard_max_bytes > 0)
			exfat_debug("'%s' supports discard", spec);
	}
#endif

#if defined(__APPLE__)
	if (!S_ISREG(stbuf.st_mode))
	{
		uint32_t block_size = 0;
		uint64_t blocks = 0;

		if (ioctl(dev->fd, DKIOCGETBLOCKSIZE, &block_size) != 0)
		{
			close(dev->fd);
			free(dev);
			exfat_error("failed to get block size");
			return NULL;
		}
		if (ioctl(dev->fd, DKIOCGETBLOCKCOUNT, &blocks) != 0)
		{
			close(dev->fd);
			free(dev);
			exfat_error("failed to get blocks count");
			return NULL;
		}
		dev->size = blocks * block_size;
	}
	else
#elif defined(__OpenBSD__)
	if (!S_ISREG(stbuf.st_mode))
	{
		struct disklabel lab;
		struct partition* pp;
		char* partition;

		if (ioctl(dev->fd, DIOCGDINFO, &lab) == -1)
		{
			close(dev->fd);
			free(dev);
			exfat_error("failed to get disklabel");
			return NULL;
		}

		/* Don't need to check that partition letter is valid as we won't get
		   this far otherwise. */
		partition = strchr(spec, '\0') - 1;
		pp = &(lab.d_partitions[*partition - 'a']);
		dev->size = DL_GETPSIZE(pp) * lab.d_secsize;

		if (pp->p_fstype != FS_NTFS)
			exfat_warn("partition type is not 0x07 (NTFS/exFAT); "
					"you can fix this with fdisk(8)");
	}
	else
#endif
	{
		/* works for Linux, FreeBSD, Solaris */
		dev->size = exfat_seek(dev, 0, SEEK_END);
		if (dev->size <= 0)
		{
			close(dev->fd);
			free(dev);
			exfat_error("failed to get size of '%s'", spec);
			return NULL;
		}
		if (exfat_seek(dev, 0, SEEK_SET) == -1)
		{
			close(dev->fd);
			free(dev);
			exfat_error("failed to seek to the beginning of '%s'", spec);
			return NULL;
		}
	}

	return dev;
}

int exfat_close(struct exfat_dev* dev)
{
	int rc = 0;

	if (close(dev->fd) != 0)
	{
		exfat_error("failed to close device: %s", strerror(errno));
		rc = -EIO;
	}
	free(dev);
	return rc;
}

int exfat_fsync(struct exfat_dev* dev)
{
	int rc = 0;

	if (fsync(dev->fd) != 0)
	{
		exfat_error("fsync failed: %s", strerror(errno));
		rc = -EIO;
	}
	return rc;
}

enum exfat_mode exfat_get_mode(const struct exfat_dev* dev)
{
	return dev->mode;
}

off_t exfat_get_size(const struct exfat_dev* dev)
{
	return dev->size;
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

ssize_t exfat_pread(struct exfat_dev* dev, void* buffer, size_t size,
		off_t offset)
{
	return pread(dev->fd, buffer, size, offset);
}

ssize_t exfat_pwrite(struct exfat_dev* dev, const void* buffer, size_t size,
		off_t offset)
{
	return pwrite(dev->fd, buffer, size, offset);
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
	if (CLUSTER_INVALID(*ef->sb, cluster))
	{
		exfat_error("invalid cluster 0x%x while reading", cluster);
		return -EIO;
	}

	loffset = offset % CLUSTER_SIZE(*ef->sb);
	remainder = MIN(size, node->size - offset);
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(*ef->sb, cluster))
		{
			exfat_error("invalid cluster 0x%x while reading", cluster);
			return -EIO;
		}
		lsize = MIN(CLUSTER_SIZE(*ef->sb) - loffset, remainder);
		if (exfat_pread(ef->dev, bufp, lsize,
					exfat_c2o(ef, cluster) + loffset) < 0)
		{
			exfat_error("failed to read cluster %#x", cluster);
			return -EIO;
		}
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	if (!(node->attrib & EXFAT_ATTRIB_DIR) && !ef->ro && !ef->noatime)
		exfat_update_atime(node);
	return MIN(size, node->size - offset) - remainder;
}

ssize_t exfat_generic_pwrite(struct exfat* ef, struct exfat_node* node,
		const void* buffer, size_t size, off_t offset)
{
	int rc;
	cluster_t cluster;
	const char* bufp = buffer;
	off_t lsize, loffset, remainder;

 	if (offset > node->size)
	{
		rc = exfat_truncate(ef, node, offset, true);
		if (rc != 0)
			return rc;
	}
  	if (offset + size > node->size)
	{
		rc = exfat_truncate(ef, node, offset + size, false);
		if (rc != 0)
			return rc;
	}
	if (size == 0)
		return 0;

	cluster = exfat_advance_cluster(ef, node, offset / CLUSTER_SIZE(*ef->sb));
	if (CLUSTER_INVALID(*ef->sb, cluster))
	{
		exfat_error("invalid cluster 0x%x while writing", cluster);
		return -EIO;
	}

	loffset = offset % CLUSTER_SIZE(*ef->sb);
	remainder = size;
	while (remainder > 0)
	{
		if (CLUSTER_INVALID(*ef->sb, cluster))
		{
			exfat_error("invalid cluster 0x%x while writing", cluster);
			return -EIO;
		}
		lsize = MIN(CLUSTER_SIZE(*ef->sb) - loffset, remainder);
		if (exfat_pwrite(ef->dev, bufp, lsize,
				exfat_c2o(ef, cluster) + loffset) < 0)
		{
			exfat_error("failed to write cluster %#x", cluster);
			return -EIO;
		}
		bufp += lsize;
		loffset = 0;
		remainder -= lsize;
		cluster = exfat_next_cluster(ef, node, cluster);
	}
	if (!(node->attrib & EXFAT_ATTRIB_DIR))
		/* directory's mtime should be updated by the caller only when it
		   creates or removes something in this directory */
		exfat_update_mtime(node);
	return size - remainder;
}

int exfat_generic_trim(struct exfat_dev* dev, off_t start, off_t end)
{
	int rc = -EOPNOTSUPP;

	if (start >= end)
		return 0;

	if (0) ;
#ifdef BLKDISCARD
	else if (dev->block_discard_granularity > 0 && dev->block_discard_max_bytes > 0)
	{
		start = ROUND_UP(start - dev->block_discard_alignment, dev->block_discard_granularity)
			+ dev->block_discard_alignment;
		end = ROUND_DOWN(end - dev->block_discard_alignment, dev->block_discard_granularity)
			+ dev->block_discard_alignment;
		rc = 0;
		while (!rc && start < end)
		{
			uint64_t range[2] = {start, MIN(end - start, dev->block_discard_max_bytes)};
			exfat_debug("BLKDISCARD(%d, %llu+%llu)", dev->fd, range[0], range[1]);
			if (range[1] >= dev->block_discard_granularity)
				rc = ioctl(dev->fd, BLKDISCARD, range) ? -errno : 0;
			start += range[1];
		}
	}
#endif
	else {
#if HAVE_DECL_FALLOCATE
		exfat_debug("FL_PUNCH_HOLE(%d, %llu+%llu)", dev->fd, start, end - start);
		rc = fallocate(dev->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
				start, end - start) ? -errno : 0;
#endif
	}

	return rc;
}
