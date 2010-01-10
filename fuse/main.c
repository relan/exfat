/*
 *  main.c
 *  FUSE-based exFAT implementation. Requires FUSE 2.6 or later.
 *
 *  Created by Andrew Nayenko on 01.09.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#include <fuse.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exfat.h>
#include <inttypes.h>

#define exfat_debug(format, ...)

#if !defined(FUSE_VERSION) || (FUSE_VERSION < 26)
	#error FUSE 2.6 or later is required
#endif

struct exfat ef;

static struct exfat_node* get_node(const struct fuse_file_info* fi)
{
	return (struct exfat_node*) (size_t) fi->fh;
}

static void set_node(struct fuse_file_info* fi, struct exfat_node* node)
{
	fi->fh = (uint64_t) (size_t) node;
}

static int fuse_exfat_getattr(const char* path, struct stat* stbuf)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[fuse_exfat_getattr] %s", path);

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

	exfat_debug("[fuse_exfat_truncate] %s, %"PRIu64, path, (uint64_t) size);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	exfat_truncate(&ef, node, size);
	exfat_put_node(&ef, node);
	return 0;
}

static int fuse_exfat_readdir(const char* path, void* buffer,
		fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi)
{
	struct exfat_node* parent;
	struct exfat_node* node;
	struct exfat_iterator it;
	int rc;
	char name[EXFAT_NAME_MAX + 1];

	exfat_debug("[fuse_exfat_readdir] %s", path);

	rc = exfat_lookup(&ef, &parent, path);
	if (rc != 0)
		return rc;
	if (!(parent->flags & EXFAT_ATTRIB_DIR))
	{
		exfat_put_node(&ef, parent);
		exfat_error("`%s' is not a directory (0x%x)", path, parent->flags);
		return -ENOTDIR;
	}

	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);

	rc = exfat_opendir(&ef, parent, &it);
	if (rc != 0)
	{
		exfat_put_node(&ef, parent);
		exfat_error("failed to open directory `%s'", path);
		return rc;
	}
	while ((node = exfat_readdir(&ef, &it)))
	{
		exfat_get_name(node, name, EXFAT_NAME_MAX);
		exfat_debug("[fuse_exfat_readdir] %s: %s, %"PRIu64" bytes, cluster %u",
				name, IS_CONTIGUOUS(*node) ? "contiguous" : "fragmented",
				(uint64_t) node->size, node->start_cluster);
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

	exfat_debug("[fuse_exfat_open] %s", path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;
	set_node(fi, node);
	return 0;
}

static int fuse_exfat_release(const char* path, struct fuse_file_info* fi)
{
	exfat_put_node(&ef, get_node(fi));
	return 0;
}

static int fuse_exfat_read(const char* path, char* buffer, size_t size,
		off_t offset, struct fuse_file_info* fi)
{
	exfat_debug("[fuse_exfat_read] %s (%zu bytes)", path, size);
	return exfat_read(&ef, get_node(fi), buffer, size, offset);
}

static int fuse_exfat_write(const char* path, const char* buffer, size_t size,
		off_t offset, struct fuse_file_info* fi)
{
	exfat_debug("[fuse_exfat_write] %s (%zu bytes)", path, size);
	return exfat_write(&ef, get_node(fi), buffer, size, offset);
}

static int fuse_exfat_unlink(const char* path)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[fuse_exfat_unlink] %s", path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	rc = exfat_unlink(&ef, node);
	exfat_put_node(&ef, node);
	return rc;
}

static int fuse_exfat_rmdir(const char* path)
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[fuse_exfat_rmdir] %s", path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	rc = exfat_rmdir(&ef, node);
	exfat_put_node(&ef, node);
	return rc;
}

static int fuse_exfat_mknod(const char* path, mode_t mode, dev_t dev)
{
	exfat_debug("[fuse_exfat_mknod] %s", path);
	return exfat_mknod(&ef, path);
}

static int fuse_exfat_mkdir(const char* path, mode_t mode)
{
	exfat_debug("[fuse_exfat_mkdir] %s", path);
	return exfat_mkdir(&ef, path);
}

static int fuse_exfat_rename(const char* old_path, const char* new_path)
{
	exfat_debug("[fuse_exfat_rename] %s => %s", old_path, new_path);
	return exfat_rename(&ef, old_path, new_path);
}

static int fuse_exfat_utimens(const char* path, const struct timespec tv[2])
{
	struct exfat_node* node;
	int rc;

	exfat_debug("[fuse_exfat_utimens] %s", path);

	rc = exfat_lookup(&ef, &node, path);
	if (rc != 0)
		return rc;

	exfat_utimes(node, tv);
	exfat_put_node(&ef, node);
	return 0;
}

static int fuse_exfat_statfs(const char* path, struct statvfs* sfs)
{
	const uint64_t block_count = le64_to_cpu(ef.sb->block_count);

	sfs->f_bsize = BLOCK_SIZE(*ef.sb);
	sfs->f_frsize = CLUSTER_SIZE(*ef.sb);
	sfs->f_blocks = block_count;
	sfs->f_bavail = block_count - ef.sb->allocated_percent * block_count / 100;
	sfs->f_bfree = sfs->f_bavail;
	sfs->f_namemax = EXFAT_NAME_MAX;

	/*
	   Below are fake values because in exFAT there is
	   a) no simple way to count files;
	   b) no such thing as inode;
	   So here we assume that inode = cluster.
	*/
	sfs->f_files = (sfs->f_blocks - sfs->f_bfree) >> ef.sb->bpc_bits;
	sfs->f_favail = sfs->f_bfree >> ef.sb->bpc_bits;
	sfs->f_ffree = sfs->f_bavail;

	return 0;
}

static void fuse_exfat_destroy(void* unused)
{
	exfat_unmount(&ef);
}

static void usage(const char* prog)
{
	fprintf(stderr, "Usage: %s <spec> <mountpoint> [-o options]\n", prog);
	exit(1);
}

static struct fuse_operations fuse_exfat_ops =
{
	.getattr	= fuse_exfat_getattr,
	.truncate	= fuse_exfat_truncate,
	.readdir	= fuse_exfat_readdir,
	.open		= fuse_exfat_open,
	.release	= fuse_exfat_release,
	.read		= fuse_exfat_read,
	.write		= fuse_exfat_write,
	.unlink		= fuse_exfat_unlink,
	.rmdir		= fuse_exfat_rmdir,
	.mknod		= fuse_exfat_mknod,
	.mkdir		= fuse_exfat_mkdir,
	.rename		= fuse_exfat_rename,
	.utimens	= fuse_exfat_utimens,
	.statfs		= fuse_exfat_statfs,
	.destroy	= fuse_exfat_destroy,
};

int main(int argc, char* argv[])
{
	struct fuse_args mount_args = FUSE_ARGS_INIT(0, NULL);
	struct fuse_args newfs_args = FUSE_ARGS_INIT(0, NULL);
	const char* spec = NULL;
	const char* mount_point = NULL;
	const char* mount_options = "";
	int debug = 0;
	struct fuse_chan* fc = NULL;
	struct fuse* fh = NULL;
	char** pp;

	printf("FUSE exfat %u.%u\n", EXFAT_VERSION_MAJOR, EXFAT_VERSION_MINOR);

	for (pp = argv + 1; *pp; pp++)
	{
		if (strcmp(*pp, "-o") == 0)
		{
			pp++;
			if (*pp == NULL)
				usage(argv[0]);
			mount_options = *pp;
		}
		else if (strcmp(*pp, "-d") == 0)
			debug = 1;
		else if (spec == NULL)
			spec = *pp;
		else if (mount_point == NULL)
			mount_point = *pp;
		else
			usage(argv[0]);
	}
	if (spec == NULL || mount_point == NULL)
		usage(argv[0]);

	/* create arguments for fuse_mount() */
	if (fuse_opt_add_arg(&mount_args, "exfat") != 0 ||
		fuse_opt_add_arg(&mount_args, "-o") != 0 ||
		fuse_opt_add_arg(&mount_args, mount_options) != 0)
		return 1;

	/* create FUSE mount point */
	fc = fuse_mount(mount_point, &mount_args);
	fuse_opt_free_args(&mount_args);
	if (fc == NULL)
		return 1;

	/* create arguments for fuse_new() */
	if (fuse_opt_add_arg(&newfs_args, "") != 0 ||
		(debug && fuse_opt_add_arg(&newfs_args, "-d") != 0))
	{
		fuse_unmount(mount_point, fc);
		return 1;
	}

	/* create new FUSE file system */
	fh = fuse_new(fc, &newfs_args, &fuse_exfat_ops,
			sizeof(struct fuse_operations), NULL);
	fuse_opt_free_args(&newfs_args);
	if (fh == NULL)
	{
		fuse_unmount(mount_point, fc);
		return 1;
	}

	/* exit session on HUP, TERM and INT signals and ignore PIPE signal */
	if (fuse_set_signal_handlers(fuse_get_session(fh)))
	{
		fuse_unmount(mount_point, fc);
		fuse_destroy(fh);
		return 1;
	}

	if (exfat_mount(&ef, spec, mount_options) != 0)
	{
		fuse_unmount(mount_point, fc);
		fuse_destroy(fh);
		return 1;
	}

	/* go to background unless "-d" option is passed */
	fuse_daemonize(debug);

	/* FUSE main loop */
	fuse_loop(fh);

	/* it's quite illogical but fuse_unmount() must be called BEFORE
	   fuse_destroy() */
	fuse_unmount(mount_point, fc);
	fuse_destroy(fh);
	return 0;
}
