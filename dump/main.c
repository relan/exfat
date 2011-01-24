/*
	main.c (08.11.10)
	Prints detailed information about exFAT volume.

	Copyright (C) 2010  Andrew Nayenko

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

#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <exfat.h>

static void print_generic_info(const struct exfat_super_block* sb)
{
	printf("Volume serial number      0x%08x\n",
			le32_to_cpu(sb->volume_serial));
	printf("FS version                       %hhu.%hhu\n",
			sb->version.major, sb->version.minor);
	printf("Block size                %10u\n",
			BLOCK_SIZE(*sb));
	printf("Cluster size              %10u\n",
			CLUSTER_SIZE(*sb));
}

static void print_block_info(const struct exfat_super_block* sb)
{
	printf("Blocks count              %10"PRIu64"\n",
			le64_to_cpu(sb->block_count));
}

static void print_cluster_info(const struct exfat_super_block* sb)
{
	printf("Clusters count            %10u\n",
			le32_to_cpu(sb->cluster_count));
}

static void print_other_info(const struct exfat_super_block* sb)
{
	printf("First block               %10"PRIu64"\n",
			le64_to_cpu(sb->block_start));
	printf("FAT first block           %10u\n",
			le32_to_cpu(sb->fat_block_start));
	printf("FAT blocks count          %10u\n",
			le32_to_cpu(sb->fat_block_count));
	printf("First cluster block       %10u\n",
			le32_to_cpu(sb->cluster_block_start));
	printf("Root directory cluster    %10u\n",
			le32_to_cpu(sb->rootdir_cluster));
	printf("Volume state                  0x%04hx\n",
			le16_to_cpu(sb->volume_state));
	printf("FATs count                %10hhu\n",
			sb->fat_count);
	printf("Drive number                    0x%02hhx\n",
			sb->drive_no);
	printf("Allocated space           %9hhu%%\n",
			sb->allocated_percent);
}

static int dump_sb(const char* spec)
{
	int fd;
	struct exfat_super_block sb;

	fd = open(spec, O_RDONLY);
	if (fd < 0)
	{
		exfat_error("failed to open `%s'", spec);
		return 1;
	}
	if (read(fd, &sb, sizeof(struct exfat_super_block))
			!= sizeof(struct exfat_super_block))
	{
		close(fd);
		exfat_error("failed to read from `%s'", spec);
		return 1;
	}
	if (memcmp(sb.oem_name, "EXFAT   ", sizeof(sb.oem_name)) != 0)
	{
		close(fd);
		exfat_error("exFAT file system is not found on `%s'", spec);
		return 1;
	}

	print_generic_info(&sb);
	print_block_info(&sb);
	print_cluster_info(&sb);
	print_other_info(&sb);

	close(fd);
	return 0;
}

static int dump_full(const char* spec)
{
	struct exfat ef;
	uint32_t free_clusters;
	uint64_t free_blocks;

	if (exfat_mount(&ef, spec, "ro") != 0)
		return 1;

	free_clusters = exfat_count_free_clusters(&ef);
	free_blocks = (uint64_t) free_clusters << ef.sb->bpc_bits;

	printf("Volume label         %15s\n", exfat_get_label(&ef));
	print_generic_info(ef.sb);
	print_block_info(ef.sb);
	printf("Free blocks               %10"PRIu64"\n", free_blocks);
	print_cluster_info(ef.sb);
	printf("Free clusters             %10u\n", free_clusters);
	print_other_info(ef.sb);

	exfat_unmount(&ef);
	return 0;
}

static void usage(const char* prog)
{
	fprintf(stderr, "Usage: %s [-s] <device>\n", prog);
	exit(1);
}

int main(int argc, char* argv[])
{
	char** pp;
	const char* spec = NULL;
	int sb_only = 0;

	for (pp = argv + 1; *pp; pp++)
	{
		if (strcmp(*pp, "-s") == 0)
			sb_only = 1;
		else if (spec == NULL)
			spec = *pp;
		else
			usage(argv[0]);
	}
	if (spec == NULL)
		usage(argv[0]);

	if (sb_only)
		return dump_sb(spec);
	else
		return dump_full(spec);
}
