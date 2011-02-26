/*
	main.c (15.08.10)
	Creates exFAT file system.

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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <exfat.h>
#include "vbr.h"
#include "fat.h"
#include "cbm.h"
#include "uct.h"
#include "rootdir.h"

#define ROUND_UP(x, d) (DIV_ROUND_UP(x, d) * (d))

struct exfat_super_block sb;
struct exfat_entry_label label_entry = {EXFAT_ENTRY_LABEL ^ EXFAT_ENTRY_VALID};
struct exfat_entry_bitmap bitmap_entry = {EXFAT_ENTRY_BITMAP};
struct exfat_entry_upcase upcase_entry = {EXFAT_ENTRY_UPCASE};

struct exfat_structure
{
	const char* name;
	int order;
	off_t (*get_alignment)(void);
	off_t (*get_size)(void);
	int (*write_data)(off_t, int);
};

static int init_sb(off_t volume_size, int sector_bits, int spc_bits,
		uint32_t volume_serial, int first_sector)
{
	uint32_t clusters_max = (volume_size >> sector_bits >> spc_bits);
	uint32_t fat_sectors = DIV_ROUND_UP(clusters_max * 4, 1 << sector_bits);
	uint32_t allocated_clusters;

	memset(&sb, 0, sizeof(struct exfat_super_block));
	sb.jump[0] = 0xeb;
	sb.jump[1] = 0x76;
	sb.jump[2] = 0x90;
	memcpy(sb.oem_name, "EXFAT   ", sizeof(sb.oem_name));
	sb.sector_start = cpu_to_le64(first_sector);
	sb.sector_count = cpu_to_le64(volume_size >> sector_bits);
	sb.fat_sector_start = cpu_to_le32(128); /* FIXME */
	sb.fat_sector_count = cpu_to_le32(ROUND_UP(
			le32_to_cpu(sb.fat_sector_start) + fat_sectors, 1 << spc_bits) -
			le32_to_cpu(sb.fat_sector_start));
	/* cluster_sector_start will be set later */
	sb.cluster_count = cpu_to_le32(clusters_max -
			((le32_to_cpu(sb.fat_sector_start) +
			  le32_to_cpu(sb.fat_sector_count)) >> spc_bits));
	/* rootdir_cluster will be set later */
	sb.volume_serial = cpu_to_le32(volume_serial);
	sb.version.major = 1;
	sb.version.minor = 0;
	sb.volume_state = cpu_to_le16(0);
	sb.sector_bits = sector_bits;
	sb.spc_bits = spc_bits;
	sb.fat_count = 1;
	sb.drive_no = 0x80;
	sb.allocated_percent = 0;
	sb.boot_signature = cpu_to_le16(0xaa55);

	allocated_clusters =
			DIV_ROUND_UP(cbm_size(), CLUSTER_SIZE(sb)) +
			DIV_ROUND_UP(uct_size(), CLUSTER_SIZE(sb)) +
			DIV_ROUND_UP(rootdir_size(), CLUSTER_SIZE(sb));
	if (clusters_max < ((le32_to_cpu(sb.fat_sector_start) +
			le32_to_cpu(sb.fat_sector_count)) >> spc_bits) +
			allocated_clusters)
	{
		exfat_error("too small volume (%"PRIu64" bytes)", volume_size);
		return 1;
	}
	exfat_print_info(&sb, le32_to_cpu(sb.cluster_count) -
			allocated_clusters);
	return 0;
}

static int erase_device(int fd)
{
	uint64_t erase_sectors = (uint64_t)
			le32_to_cpu(sb.fat_sector_start) +
			le32_to_cpu(sb.fat_sector_count) +
			DIV_ROUND_UP(cbm_size(), 1 << sb.sector_bits) +
			DIV_ROUND_UP(uct_size(), 1 << sb.sector_bits) +
			DIV_ROUND_UP(rootdir_size(), 1 << sb.sector_bits);
	uint64_t i;
	void* sector;

	if (lseek(fd, 0, SEEK_SET) == (off_t) -1)
	{
		exfat_error("seek failed");
		return 1;
	}

	sector = malloc(SECTOR_SIZE(sb));
	if (sector == NULL)
	{
		exfat_error("failed to allocate erase sector");
		return 1;
	}
	memset(sector, 0, SECTOR_SIZE(sb));

	for (i = 0; i < erase_sectors; i++)
	{
		if (write(fd, sector, SECTOR_SIZE(sb)) == -1)
		{
			free(sector);
			exfat_error("failed to erase sector %"PRIu64, i);
			return 1;
		}
		if (i * 100 / erase_sectors != (i + 1) * 100 / erase_sectors)
		{
			printf("\b\b\b%2"PRIu64"%%", (i + 1) * 100 / erase_sectors);
			fflush(stdout);
		}
	}
	free(sector);
	return 0;
}

/*
 * exFAT layout:
 * - Volume Boot Record (VBR)
 *   - Main Boot Sector (MBR)
 *   - Main Extended Boot Sectors (MEBS)
 *   - OEM Parameters
 *   - Reserved sector
 *   - Checksum sector
 * - Volume Boot Record copy
 * - File Allocation Table (FAT)
 * - Clusters heap
 *   - Clusters bitmap
 *   - Upper case table
 *   - Root directory
 */
#define FS_OBJECT(order, name) \
	{#name, order, name##_alignment, name##_size, name##_write}
static struct exfat_structure structures[] =
{
	FS_OBJECT(3, vbr),
	FS_OBJECT(3, vbr),
	FS_OBJECT(2, fat),
	FS_OBJECT(1, cbm),
	FS_OBJECT(1, uct),
	FS_OBJECT(1, rootdir)
};
#undef FS_OBJECT

static off_t write_structure(int fd, struct exfat_structure* structure,
		off_t current)
{
	off_t alignment = structure->get_alignment();
	off_t base = ROUND_UP(current, alignment);

	if (lseek(fd, base, SEEK_SET) == (off_t) -1)
	{
		exfat_error("seek to %"PRIu64" failed", base);
		return -1;
	}
	if (structure->order > 0)
	{
		int rc = structure->write_data(base, fd);
		if (rc != 0)
		{
			exfat_error("%s creation failed: %s", structure->name,
					strerror(rc));
			return -1;
		}
		structure->order--;
	}
	return base + structure->get_size();
}

static int write_structures(int fd)
{
	off_t current;
	size_t i;
	int remainder;

	do
	{
		current = 0;
		remainder = 0;
		for (i = 0; i < sizeof(structures) / sizeof(structures[0]); i++)
		{
			current = write_structure(fd, &structures[i], current);
			if (current == (off_t) -1)
				return 1;
			remainder += structures[i].order;
		}
	}
	while (remainder > 0);
	return 0;
}

static int get_spc_bits(int user_defined, off_t volume_size)
{
	if (user_defined != -1)
		return user_defined;

	if (volume_size < 256ull * 1024 * 1024)
		return 3;	/* 4 KB */
	else if (volume_size < 32ull * 1024 * 1024 * 1024)
		return 6;	/* 32 KB */
	else
		return 8;	/* 128 KB */
}

static int set_volume_label(int fd, const char* volume_label)
{
	le16_t tmp[EXFAT_ENAME_MAX + 1];

	if (volume_label == NULL)
		return 0;

	memset(tmp, 0, sizeof(tmp));
	if (utf8_to_utf16(tmp, volume_label, EXFAT_ENAME_MAX,
				strlen(volume_label)) != 0)
	{
		close(fd);
		return 1;
	}
	memcpy(label_entry.name, tmp, EXFAT_ENAME_MAX * sizeof(le16_t));
	label_entry.length = utf16_length(tmp);
	label_entry.type |= EXFAT_ENTRY_VALID;
	return 0;
}

static uint32_t get_volume_serial(uint32_t user_defined)
{
	struct timeval now;

	if (user_defined != 0)
		return user_defined;

	if (gettimeofday(&now, NULL) != 0)
		return 0;
	return (now.tv_sec << 20) | now.tv_usec;
}

static int mkfs(const char* spec, int sector_bits, int spc_bits,
		const char* volume_label, uint32_t volume_serial, int first_sector)
{
	int fd;
	off_t volume_size;
	char spec_abs[PATH_MAX];

	if (realpath(spec, spec_abs) == NULL)
	{
		exfat_error("failed to get absolute path for `%s'", spec);
		return 1;
	}

	fd = open(spec_abs, O_RDWR);
	if (fd < 0)
	{
		exfat_error("failed to open special file `%s'", spec_abs);
		return 1;
	}

	volume_size = lseek(fd, 0, SEEK_END);
	if (volume_size == (off_t) -1)
	{
		close(fd);
		exfat_error("seek failed");
		return 1;
	}
	spc_bits = get_spc_bits(spc_bits, volume_size);

	if (set_volume_label(fd, volume_label) != 0)
	{
		close(fd);
		return 1;
	}

	volume_serial = get_volume_serial(volume_serial);
	if (volume_serial == 0)
	{
		close(fd);
		exfat_error("failed to get current time to form volume id");
		return 1;
	}

	if (init_sb(volume_size, sector_bits, spc_bits, volume_serial,
				first_sector) != 0)
	{
		close(fd);
		return 1;
	}

	printf("Creating... %2u%%", 0);
	fflush(stdout);
	if (erase_device(fd) != 0)
	{
		close(fd);
		return 1;
	}
	if (write_structures(fd) != 0)
	{
		close(fd);
		return 1;
	}
	puts("\b\b\b\bdone.");

	printf("Flushing... ");
	fflush(stdout);
	if (fsync(fd) < 0)
	{
		close(fd);
		exfat_error("fsync failed for `%s'", spec_abs);
		return 1;
	}
	puts("done.");
	if (close(fd) < 0)
	{
		exfat_error("close failed for `%s'", spec_abs);
		return 1;
	}
	printf("File system created successfully.\n");
	return 0;
}

static int logarithm2(int n)
{
	int i;

	for (i = 0; i < sizeof(int) * CHAR_BIT - 1; i++)
		if ((1 << i) == n)
			return i;
	return -1;
}

static void usage(const char* prog)
{
	fprintf(stderr, "Usage: %s [-i volume-id] [-n label] "
			"[-p partition-first-sector] "
			"[-s sectors-per-cluster] <device>\n", prog);
	exit(1);
}

int main(int argc, char* argv[])
{
	const char* spec = NULL;
	char** pp;
	int spc_bits = -1;
	const char* volume_label = NULL;
	uint32_t volume_serial = 0;
	int first_sector = 0;

	for (pp = argv + 1; *pp; pp++)
	{
		if (strcmp(*pp, "-s") == 0)
		{
			pp++;
			if (*pp == NULL)
				usage(argv[0]);
			spc_bits = logarithm2(atoi(*pp));
			if (spc_bits < 0)
			{
				exfat_error("invalid option value: `%s'", *pp);
				return 1;
			}
		}
		else if (strcmp(*pp, "-n") == 0)
		{
			pp++;
			if (*pp == NULL)
				usage(argv[0]);
			volume_label = *pp;
			/* TODO check length */
		}
		else if (strcmp(*pp, "-i") == 0)
		{
			pp++;
			if (*pp == NULL)
				usage(argv[0]);
			volume_serial = strtol(*pp, NULL, 16);
		}
		else if (strcmp(*pp, "-p") == 0)
		{
			pp++;
			if (*pp == NULL)
				usage(argv[0]);
			first_sector = atoi(*pp);
		}
		else if (**pp == '-')
		{
			exfat_error("unrecognized option `%s'", *pp);
			return 1;
		}
		else
			spec = *pp;
	}
	if (spec == NULL)
		usage(argv[0]);

	printf("mkexfatfs %u.%u.%u\n",
			EXFAT_VERSION_MAJOR, EXFAT_VERSION_MINOR, EXFAT_VERSION_PATCH);

	return mkfs(spec, 9, spc_bits, volume_label, volume_serial, first_sector);
}
