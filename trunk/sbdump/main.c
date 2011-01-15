/*
	main.c (08.11.10)
	A small utility that prints exFAT super block contents in human-readable
	format. Useless for end users.

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

#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <exfat.h>

static void dump_sb(const struct exfat_super_block* sb)
{
	printf("First block               %8"PRIu64"\n",
			le64_to_cpu(sb->block_start));
	printf("Blocks count              %8"PRIu64"\n",
			le64_to_cpu(sb->block_count));
	printf("FAT first block           %8u\n",
			le32_to_cpu(sb->fat_block_start));
	printf("FAT blocks count          %8u\n",
			le32_to_cpu(sb->fat_block_count));
	printf("First cluster block       %8u\n",
			le32_to_cpu(sb->cluster_block_start));
	printf("Clusters count            %8u\n",
			le32_to_cpu(sb->cluster_count));
	printf("Root directory cluster    %8u\n",
			le32_to_cpu(sb->rootdir_cluster));
	printf("Volume serial number    0x%08x\n",
			le32_to_cpu(sb->volume_serial));
	printf("FS version                     %hhu.%hhu\n",
			sb->version.major, sb->version.minor);
	printf("Volume state                0x%04hx\n",
			le16_to_cpu(sb->volume_state));
	printf("Block size                %8u\n",
			BLOCK_SIZE(*sb));
	printf("Cluster size              %8u\n",
			CLUSTER_SIZE(*sb));
	printf("FATs count                %8hhu\n",
			sb->fat_count);
	printf("Drive number                  0x%02hhx\n",
			sb->drive_no);
	printf("Allocated space           %7hhu%%\n",
			sb->allocated_percent);
}

int main(int argc, char* argv[])
{
	int fd;
	struct exfat_super_block sb;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <device>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0)
	{
		fprintf(stderr, "failed to open `%s'\n", argv[1]);
		return 1;
	}
	if (read(fd, &sb, sizeof(struct exfat_super_block))
			!= sizeof(struct exfat_super_block))
	{
		close(fd);
		fprintf(stderr, "failed to read from `%s'.\n", argv[1]);
		return 1;
	}
	if (memcmp(sb.oem_name, "EXFAT   ", sizeof(sb.oem_name)) != 0)
	{
		close(fd);
		fprintf(stderr, "exFAT file system is not found on `%s'.\n", argv[1]);
		return 1;
	}
	dump_sb(&sb);
	close(fd);
	return 0;
}
