/*
 *  main.c
 *  exFAT file system checker.
 *
 *  Created by Andrew Nayenko on 02.09.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#include <stdio.h>
#include <string.h>
#include <exfat.h>
#include <exfatfs.h>
#include <inttypes.h>

#define exfat_debug(format, ...)

#define MB (1024 * 1024)

uint64_t files_count, directories_count;

static uint64_t bytes2mb(uint64_t bytes)
{
	return (bytes + MB / 2) / MB;
}

static void sbck(const struct exfat* ef)
{
	const uint32_t block_size = (1 << ef->sb->block_bits); /* in bytes */
	const uint32_t cluster_size = CLUSTER_SIZE(*ef->sb); /* in bytes */
	const uint64_t total = le32_to_cpu(ef->sb->cluster_count) * cluster_size;

#if 0 /* low-level info */
	printf("First block           %8"PRIu64"\n",
			le64_to_cpu(ef->sb->block_start));
	printf("Blocks count          %8"PRIu64"\n",
			le64_to_cpu(ef->sb->block_count));
	printf("FAT first block       %8u\n",
			le32_to_cpu(ef->sb->fat_block_start));
	printf("FAT blocks count      %8u\n",
			le32_to_cpu(ef->sb->fat_block_count));
	printf("First cluster block   %8u\n",
			le32_to_cpu(ef->sb->cluster_block_start));
	printf("Clusters count        %8u\n",
			le32_to_cpu(ef->sb->cluster_count));
	printf("First cluster of root %8u\n",
			le32_to_cpu(ef->sb->rootdir_cluster));
#endif
	printf("Block size            %8u bytes\n", block_size);
	printf("Cluster size          %8u bytes\n", cluster_size);
	printf("Total space           %8"PRIu64" MB\n", bytes2mb(total));
	printf("Used space            %8"PRIu64" MB (%hhu%%)\n",
			bytes2mb(total * ef->sb->allocated_percent / 100),
			ef->sb->allocated_percent);
	printf("Free space            %8"PRIu64" MB (%hhu%%)\n",
			bytes2mb(total * (100 - ef->sb->allocated_percent) / 100),
			100 - ef->sb->allocated_percent);
}

static void dirck(struct exfat* ef, const char* path)
{
	struct exfat_node* parent;
	struct exfat_node* node;
	struct exfat_iterator it;
	char subpath[EXFAT_NAME_MAX + 1];

	if (exfat_lookup(ef, &parent, path) != 0)
		exfat_bug("directory `%s' is not found", path);
	if (!(parent->flags & EXFAT_ATTRIB_DIR))
		exfat_bug("`%s' is not a directory (0x%x)", path, parent->flags);

	exfat_opendir(parent, &it);
	while (exfat_readdir(ef, parent, &node, &it) == 0)
	{
		exfat_debug("%s/%s: %s, %llu bytes, cluster %u", path, node->name,
				IS_CONTIGUOUS(*node) ? "contiguous" : "fragmented",
				node->size, node->start_cluster);
		if (node->flags & EXFAT_ATTRIB_DIR)
		{
			directories_count++;
			strcpy(subpath, path);
			strcat(subpath, "/");
			exfat_get_name(node, subpath + strlen(subpath),
					EXFAT_NAME_MAX - strlen(subpath));
			dirck(ef, subpath);
		}
		else
			files_count++;
		exfat_put_node(node);
	}
	exfat_closedir(&it);
	exfat_put_node(parent);
}

static void fsck(struct exfat* ef)
{
	sbck(ef);
	dirck(ef, "");
}

int main(int argc, char* argv[])
{
	struct exfat ef;

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <spec>\n", argv[0]);
		return 1;
	}
	printf("exfatck %u.%u\n",
			EXFAT_VERSION_MAJOR, EXFAT_VERSION_MINOR);

	if (exfat_mount(&ef, argv[1]) != 0)
		return 1;

	printf("Checking file system on %s.\n", argv[1]);
	fsck(&ef);
	exfat_unmount(&ef);
	printf("Totally %"PRIu64" directories and %"PRIu64" files.\n",
			directories_count, files_count);

	fputs("File system checking finished: ", stdout);
	if (exfat_errors == 0)
		puts("seems OK.");
	else
		printf("%d ERROR%s FOUND!!!\n", exfat_errors,
				exfat_errors > 1 ? "S WERE" : " WAS");
	return 0;
}
