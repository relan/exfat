/*
	main.c (01.09.09)
	FUSE-based exFAT implementation. Requires FUSE 2.6 or later.

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

#include <exfat.h>
#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#ifndef DEBUG
	#define exfat_debug(format, ...)
#endif

#if !defined(FUSE_VERSION) || (FUSE_VERSION < 26)
	#error FUSE 2.6 or later is required
#endif

const char* default_options = "ro_fallback,allow_other,blkdev,big_writes,"
		"default_permissions";

struct exfat ef;

static struct exfat_node* get_node(const struct fuse_file_info* fi)
{
	return (struct exfat_node*) (size_t) fi->fh;
}

static void set_node(struct fuse_file_info* fi, struct exfat_node* node)
{
	fi->fh = (uint64_t) (size_t) node;
	fi->keep_cache = 1;
}

static int fuse_exfat_getattr(const char* path, struct stat* stbuf)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	exfat_stat(&ef, node, stbuf);
	exfat_put_node(&ef, node);
	return 0;
}

static int fuse_exfat_truncate(const char* path, off_t size)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s, %"PRId64, __func__, path, size);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	rc = exfat_truncate(&ef, node, size, true);
	if (rc != 0)
	{
		exfat_flush_node(&ef, node);	/* ignore return code */
		exfat_put_node(&ef, node);
		return rc;
	}
	rc = exfat_flush_node(&ef, node);
	exfat_put_node(&ef, node);
	return rc;
}

static int fuse_exfat_readdir(const char* path, void* buffer,
		fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi)
{
	struct exfat_node* parent;
	struct exfat_node* node;
	struct exfat_iterator it;
	int rc;
	char name[EXFAT_UTF8_NAME_BUFFER_MAX];

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &parent, path);
	if (rc != 0)
		return rc;
	if (!(parent->attrib & EXFAT_ATTRIB_DIR))
	{
		exfat_put_node(&ef, parent);
		exfat_error("'%s' is not a directory (%#hx)", path, parent->attrib);
		return -ENOTDIR;
	}

	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

	rc = exfat_opendir(&ef, parent, &it);
	if (rc != 0)
	{
		exfat_put_node(&ef, parent);
		exfat_error("failed to open directory '%s'", path);
		return rc;
	}
	while ((node = exfat_readdir(&it)))
	{
		exfat_get_name(node, name);
		exfat_debug("[%s] %s: %s, %"PRId64" bytes, cluster 0x%x", __func__,
				name, node->is_contiguous ? "contiguous" : "fragmented",
				node->size, node->start_cluster);
		filler(buffer, name, NULL, 0);
		exfat_put_node(&ef, node);
	}
	exfat_closedir(&ef, &it);
	exfat_put_node(&ef, parent);
	return 0;
}

static int fuse_exfat_open(const char* path, struct fuse_file_info* fi)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;
	set_node(fi, node);
	return 0;
}

static int fuse_exfat_create(const char* path, mode_t mode,
		struct fuse_file_info* fi)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s 0%ho", __func__, path, mode);

	rc = exfat_mknod(&ef, path);
	if (rc != 0)
		return rc;
	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;
	set_node(fi, node);
	return 0;
}

static int fuse_exfat_release(const char* path, struct fuse_file_info* fi)
{
	/*
	   This handler is called by FUSE on close() syscall. If the FUSE
	   implementation does not call flush handler, we will flush node here.
	   But in this case we will not be able to return an error to the caller.
	   See fuse_exfat_flush() below.
	*/
	exfat_debug("[%s] %s", __func__, path);
 	exfat_flush_node(&ef, get_node(fi));
	exfat_put_node(&ef, get_node(fi));
	return 0; /* FUSE ignores this return value */
}

static int fuse_exfat_flush(const char* path, struct fuse_file_info* fi)
{
	/*
	   This handler may be called by FUSE on close() syscall. FUSE also deals
	   with removals of open files, so we don't free clusters on close but
	   only on rmdir and unlink. If the FUSE implementation does not call this
	   handler we will flush node on release. See fuse_exfat_relase() above.
	*/
	exfat_debug("[%s] %s", __func__, path);
	return exfat_flush_node(&ef, get_node(fi));
}

static int fuse_exfat_fsync(const char* path, int datasync,
		struct fuse_file_info *fi)
{
	int rc;

	exfat_debug("[%s] %s", __func__, path);
	rc = exfat_flush_nodes(&ef);
	if (rc != 0)
		return rc;
	rc = exfat_flush(&ef);
	if (rc != 0)
		return rc;
	return exfat_fsync(ef.dev);
}

static int fuse_exfat_read(const char* path, char* buffer, size_t size,
		off_t offset, struct fuse_file_info* fi)
{
	exfat_debug("[%s] %s (%zu bytes)", __func__, path, size);
	return exfat_generic_pread(&ef, get_node(fi), buffer, size, offset);
}

static int fuse_exfat_write(const char* path, const char* buffer, size_t size,
		off_t offset, struct fuse_file_info* fi)
{
	exfat_debug("[%s] %s (%zu bytes)", __func__, path, size);
	return exfat_generic_pwrite(&ef, get_node(fi), buffer, size, offset);
}

static int fuse_exfat_unlink(const char* path)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	rc = exfat_unlink(&ef, node);
	exfat_put_node(&ef, node);
	if (rc != 0)
		return rc;
	return exfat_cleanup_node(&ef, node);
}

static int fuse_exfat_rmdir(const char* path)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	rc = exfat_rmdir(&ef, node);
	exfat_put_node(&ef, node);
	if (rc != 0)
		return rc;
	return exfat_cleanup_node(&ef, node);
}

static int fuse_exfat_mknod(const char* path, mode_t mode, dev_t dev)
{
	exfat_debug("[%s] %s 0%ho", __func__, path, mode);
	return exfat_mknod(&ef, path);
}

static int fuse_exfat_mkdir(const char* path, mode_t mode)
{
	exfat_debug("[%s] %s 0%ho", __func__, path, mode);
	return exfat_mkdir(&ef, path);
}

static int fuse_exfat_rename(const char* old_path, const char* new_path)
{
	exfat_debug("[%s] %s => %s", __func__, old_path, new_path);
	return exfat_rename(&ef, old_path, new_path);
}

static int fuse_exfat_utimens(const char* path, const struct timespec tv[2])
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[%s] %s", __func__, path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	exfat_utimes(node, tv);
	rc = exfat_flush_node(&ef, node);
	exfat_put_node(&ef, node);
	return rc;
}

static int fuse_exfat_bmap(const char* path, size_t blocksize, uint64_t* idx)
{
	struct exfat_node* node;
	int rc;
	size_t count;
	uint64_t extra;
	cluster_t cluster;

	exfat_debug("[%s] %s @ %zu x %"PRIu64, __func__, path, blocksize, *idx);

	if (0 >= blocksize || blocksize > CLUSTER_SIZE(*ef.sb))
		return -EINVAL;

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	count = (blocksize * *idx) / CLUSTER_SIZE(*ef.sb);
	extra = (blocksize * *idx) % CLUSTER_SIZE(*ef.sb);
	cluster = exfat_advance_cluster(&ef, node, count);
	if (CLUSTER_INVALID(*ef.sb, cluster))
		return -EINVAL;

	*idx = (exfat_c2o(&ef, cluster) + extra) / blocksize;
	rc = 0;

	exfat_put_node(&ef, node);
	return rc;
}

#if HAVE_STRUCT_FSTRIM_RANGE
static int fuse_exfat_fstrim(struct fstrim_range* data)
{
	off_t trim_start, trim_end;
	off_t used_start, used_end;
	off_t trimmed = 0;
	int rc = 0;

	exfat_debug("[%s] start=%"PRIu64" len=%"PRIu64" minlen=%"PRIu64,
			__func__, data->start, data->len, data->minlen);

	trim_start = DIV_ROUND_UP(data->start, SECTOR_SIZE(*ef.sb));
	trim_end = MIN(data->start + data->len, exfat_get_size(ef.dev)) / SECTOR_SIZE(*ef.sb);

	while (trim_start < trim_end)
	{
		used_start = used_end = trim_start;
		if (exfat_find_used_sectors(&ef, &used_start, &used_end))
			used_start = used_end = trim_end;
		if (trim_start < used_start && (used_start - trim_start) * CLUSTER_SIZE(*ef.sb) >= data->minlen)
		{
			exfat_debug("[%s] %zu-%zu", __func__, trim_start, used_start);
			trimmed += used_start - trim_start;
			rc = exfat_generic_trim(ef.dev,
					trim_start * SECTOR_SIZE(*ef.sb),
					used_start * SECTOR_SIZE(*ef.sb));
		}
		if (rc)
			break;
		trim_start = used_end;
	}

        data->len = trimmed * CLUSTER_SIZE(*ef.sb);
	return rc;
}
#endif

#ifdef FUSE_CAP_IOCTL_DIR
static int fuse_exfat_ioctl(const char* path, int cmd, void* arg,
		struct fuse_file_info* fi, unsigned int flags, void* data)
{
	exfat_debug("[%s] %s 0x%x", __func__, path, cmd);

	switch (cmd)
	{
#if HAVE_DECL_FITRIM && HAVE_STRUCT_FSTRIM_RANGE
	case FITRIM:
		return fuse_exfat_fstrim(data);
#endif
	default:
		return -EINVAL;
	}
}
#endif

static int fuse_exfat_chmod(const char* path, mode_t mode)
{
	const mode_t VALID_MODE_MASK = S_IFREG | S_IFDIR |
			S_IRWXU | S_IRWXG | S_IRWXO;

	exfat_debug("[%s] %s 0%ho", __func__, path, mode);
	if (mode & ~VALID_MODE_MASK)
		return -EPERM;
	return 0;
}

static int fuse_exfat_chown(const char* path, uid_t uid, gid_t gid)
{
	exfat_debug("[%s] %s %u:%u", __func__, path, uid, gid);
	if (uid != ef.uid || gid != ef.gid)
		return -EPERM;
	return 0;
}

static int fuse_exfat_statfs(const char* path, struct statvfs* sfs)
{
	exfat_debug("[%s]", __func__);

	sfs->f_bsize = CLUSTER_SIZE(*ef.sb);
	sfs->f_frsize = CLUSTER_SIZE(*ef.sb);
	sfs->f_blocks = le64_to_cpu(ef.sb->sector_count) >> ef.sb->spc_bits;
	sfs->f_bavail = exfat_count_free_clusters(&ef);
	sfs->f_bfree = sfs->f_bavail;
	sfs->f_namemax = EXFAT_NAME_MAX;

	/*
	   Below are fake values because in exFAT there is
	   a) no simple way to count files;
	   b) no such thing as inode;
	   So here we assume that inode = cluster.
	*/
	sfs->f_files = le32_to_cpu(ef.sb->cluster_count);
	sfs->f_favail = sfs->f_bfree >> ef.sb->spc_bits;
	sfs->f_ffree = sfs->f_bavail;

	return 0;
}

static void* fuse_exfat_init(struct fuse_conn_info* fci)
{
	exfat_debug("[%s]", __func__);
#ifdef FUSE_CAP_BIG_WRITES
	fci->want |= FUSE_CAP_BIG_WRITES;
#endif
#ifdef FUSE_CAP_IOCTL_DIR
	fci->want |= FUSE_CAP_IOCTL_DIR;
#endif
	return NULL;
}

static void fuse_exfat_destroy(void* unused)
{
	exfat_debug("[%s]", __func__);
	exfat_unmount(&ef);
}

static void usage(const char* prog)
{
	fprintf(stderr, "Usage: %s [-d] [-o options] [-V] <device> <dir>\n", prog);
	exit(1);
}

static struct fuse_operations fuse_exfat_ops =
{
	.getattr	= fuse_exfat_getattr,
	.truncate	= fuse_exfat_truncate,
	.readdir	= fuse_exfat_readdir,
	.open		= fuse_exfat_open,
	.create		= fuse_exfat_create,
	.release	= fuse_exfat_release,
	.flush		= fuse_exfat_flush,
	.fsync		= fuse_exfat_fsync,
	.fsyncdir	= fuse_exfat_fsync,
	.read		= fuse_exfat_read,
	.write		= fuse_exfat_write,
	.unlink		= fuse_exfat_unlink,
	.rmdir		= fuse_exfat_rmdir,
	.mknod		= fuse_exfat_mknod,
	.mkdir		= fuse_exfat_mkdir,
	.rename		= fuse_exfat_rename,
	.utimens	= fuse_exfat_utimens,
	.bmap           = fuse_exfat_bmap,
#ifdef FUSE_CAP_IOCTL_DIR
	.ioctl          = fuse_exfat_ioctl,
#endif
	.chmod		= fuse_exfat_chmod,
	.chown		= fuse_exfat_chown,
	.statfs		= fuse_exfat_statfs,
	.init		= fuse_exfat_init,
	.destroy	= fuse_exfat_destroy,
};

static char* add_option(char* options, const char* name, const char* value)
{
	size_t size;
	char* optionsf = options;

	if (value)
		size = strlen(options) + strlen(name) + strlen(value) + 3;
	else
		size = strlen(options) + strlen(name) + 2;

	options = realloc(options, size);
	if (options == NULL)
	{
		free(optionsf);
		exfat_error("failed to reallocate options string");
		return NULL;
	}
	strcat(options, ",");
	strcat(options, name);
	if (value)
	{
		strcat(options, "=");
		strcat(options, value);
	}
	return options;
}

static void escape(char* escaped, const char* orig)
{
	do
	{
		if (*orig == ',' || *orig == '\\')
			*escaped++ = '\\';
	}
	while ((*escaped++ = *orig++));
}

static char* add_fsname_option(char* options, const char* spec)
{
	/* escaped string cannot be more than twice as big as the original one */
	char* escaped = malloc(strlen(spec) * 2 + 1);

	if (escaped == NULL)
	{
		free(options);
		exfat_error("failed to allocate escaped string for %s", spec);
		return NULL;
	}

	/* on some platforms (e.g. Android, Solaris) device names can contain
	   commas */
	escape(escaped, spec);
	options = add_option(options, "fsname", escaped);
	free(escaped);
	return options;
}

static char* add_user_option(char* options)
{
	struct passwd* pw;

	if (getuid() == 0)
		return options;

	pw = getpwuid(getuid());
	if (pw == NULL || pw->pw_name == NULL)
	{
		free(options);
		exfat_error("failed to determine username");
		return NULL;
	}
	return add_option(options, "user", pw->pw_name);
}

static char* add_blksize_option(char* options, long cluster_size)
{
	long page_size = sysconf(_SC_PAGESIZE);
	char blksize[20];

	if (page_size < 1)
		page_size = 0x1000;

	snprintf(blksize, sizeof(blksize), "%ld", MIN(page_size, cluster_size));
	return add_option(options, "blksize", blksize);
}

static char* add_fuse_options(char* options, const char* spec)
{
	options = add_fsname_option(options, spec);
	if (options == NULL)
		return NULL;
	options = add_user_option(options);
	if (options == NULL)
		return NULL;
	options = add_blksize_option(options, CLUSTER_SIZE(*ef.sb));
	if (options == NULL)
		return NULL;

	return options;
}

int main(int argc, char* argv[])
{
	struct fuse_args mount_args = FUSE_ARGS_INIT(0, NULL);
	struct fuse_args newfs_args = FUSE_ARGS_INIT(0, NULL);
	const char* spec = NULL;
	const char* mount_point = NULL;
	char* mount_options;
	int debug = 0;
	struct fuse_chan* fc = NULL;
	struct fuse* fh = NULL;
	int opt;

	printf("FUSE exfat %s\n", VERSION);

	mount_options = strdup(default_options);
	if (mount_options == NULL)
	{
		exfat_error("failed to allocate options string");
		return 1;
	}

	while ((opt = getopt(argc, argv, "dno:Vv")) != -1)
	{
		switch (opt)
		{
		case 'd':
			debug = 1;
			break;
		case 'n':
			break;
		case 'o':
			mount_options = add_option(mount_options, optarg, NULL);
			if (mount_options == NULL)
				return 1;
			break;
		case 'V':
			free(mount_options);
			puts("Copyright (C) 2010-2018  Andrew Nayenko");
			return 0;
		case 'v':
			break;
		default:
			free(mount_options);
			usage(argv[0]);
			break;
		}
	}
	if (argc - optind != 2)
	{
		free(mount_options);
		usage(argv[0]);
	}
	spec = argv[optind];
	mount_point = argv[optind + 1];

	if (exfat_mount(&ef, spec, mount_options) != 0)
	{
		free(mount_options);
		return 1;
	}

	if (ef.ro == -1) /* read-only fallback was used */
	{
		mount_options = add_option(mount_options, "ro", NULL);
		if (mount_options == NULL)
		{
			exfat_unmount(&ef);
			return 1;
		}
	}

	mount_options = add_fuse_options(mount_options, spec);
	if (mount_options == NULL)
	{
		exfat_unmount(&ef);
		return 1;
	}

	/* create arguments for fuse_mount() */
	if (fuse_opt_add_arg(&mount_args, "exfat") != 0 ||
		fuse_opt_add_arg(&mount_args, "-o") != 0 ||
		fuse_opt_add_arg(&mount_args, mount_options) != 0)
	{
		exfat_unmount(&ef);
		free(mount_options);
		return 1;
	}

	free(mount_options);

	/* create FUSE mount point */
	fc = fuse_mount(mount_point, &mount_args);
	fuse_opt_free_args(&mount_args);
	if (fc == NULL)
	{
		exfat_unmount(&ef);
		return 1;
	}

	/* create arguments for fuse_new() */
	if (fuse_opt_add_arg(&newfs_args, "") != 0 ||
		(debug && fuse_opt_add_arg(&newfs_args, "-d") != 0))
	{
		fuse_unmount(mount_point, fc);
		exfat_unmount(&ef);
		return 1;
	}

	/* create new FUSE file system */
	fh = fuse_new(fc, &newfs_args, &fuse_exfat_ops,
			sizeof(struct fuse_operations), NULL);
	fuse_opt_free_args(&newfs_args);
	if (fh == NULL)
	{
		fuse_unmount(mount_point, fc);
		exfat_unmount(&ef);
		return 1;
	}

	/* exit session on HUP, TERM and INT signals and ignore PIPE signal */
	if (fuse_set_signal_handlers(fuse_get_session(fh)) != 0)
	{
		fuse_unmount(mount_point, fc);
		fuse_destroy(fh);
		exfat_unmount(&ef);
		exfat_error("failed to set signal handlers");
		return 1;
	}

	/* go to background (unless "-d" option is passed) and run FUSE
	   main loop */
	if (fuse_daemonize(debug) == 0)
	{
		if (fuse_loop(fh) != 0)
			exfat_error("FUSE loop failure");
	}
	else
		exfat_error("failed to daemonize");

	fuse_remove_signal_handlers(fuse_get_session(fh));
	/* note that fuse_unmount() must be called BEFORE fuse_destroy() */
	fuse_unmount(mount_point, fc);
	fuse_destroy(fh);
	return 0;
}
