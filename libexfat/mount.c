/*
 *  mount.c
 *  exFAT file system implementation library.
 *
 *  Created by Andrew Nayenko on 22.10.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#include "exfat.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#define _XOPEN_SOURCE /* for tzset() in Linux */
#include <time.h>

static uint64_t rootdir_size(const struct exfat* ef)
{
	uint64_t clusters = 0;
	cluster_t rootdir_cluster = le32_to_cpu(ef->sb->rootdir_cluster);

	while (!CLUSTER_INVALID(rootdir_cluster))
	{
		clusters++;
		/* root directory cannot be contiguous because there is no flag
		   to indicate this */
		rootdir_cluster = exfat_next_cluster(ef, rootdir_cluster, 0);
	}
	return clusters * CLUSTER_SIZE(*ef->sb);
}

int exfat_mount(struct exfat* ef, const char* spec)
{
	tzset();

	ef->sb = malloc(sizeof(struct exfat_super_block));
	if (ef->sb == NULL)
	{
		exfat_error("memory allocation failed");
		return -ENOMEM;
	}

	ef->fd = open(spec, O_RDWR);
	if (ef->fd < 0)
	{
		free(ef->sb);
		exfat_error("failed to open `%s'", spec);
		return -EIO;
	}

	exfat_read_raw(ef->sb, sizeof(struct exfat_super_block), 0, ef->fd);
	if (memcmp(ef->sb->oem_name, "EXFAT   ", 8) != 0)
	{
		close(ef->fd);
		free(ef->sb);
		exfat_error("exFAT file system is not found");
		return -EIO;
	}

	ef->upcase = NULL;
	ef->upcase_chars = 0;
	ef->rootdir_size = rootdir_size(ef);

	ef->root = malloc(sizeof(struct exfat_node));
	if (ef->root == NULL)
	{
		close(ef->fd);
		free(ef->sb);
		exfat_error("failed to allocate root node");
		return -ENOMEM;
	}
	memset(ef->root, 0, sizeof(struct exfat_node));
	ef->root->flags = EXFAT_ATTRIB_DIR;
	ef->root->size = ef->rootdir_size;
	ef->root->start_cluster = le32_to_cpu(ef->sb->rootdir_cluster);
	ef->root->name[0] = cpu_to_le16('\0');
	/* exFAT does not have time attributes for the root directory */
	ef->root->mtime = 0;
	ef->root->atime = 0;
	/* always keep at least 1 reference to the root node */
	exfat_get_node(ef->root);

	return 0;
}

void exfat_unmount(struct exfat* ef)
{
	exfat_put_node(ef->root);
	exfat_reset_cache(ef);
	ef->root = NULL;
	close(ef->fd);
	ef->fd = 0;
	free(ef->sb);
	ef->sb = NULL;
	free(ef->upcase);
	ef->upcase = NULL;
	ef->upcase_chars = 0;
}
