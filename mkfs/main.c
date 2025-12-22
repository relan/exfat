/*
	main.c (15.08.10)
	Creates exFAT file system.

	Free exFAT implementation.
	Copyright (C) 2011-2023  Andrew Nayenko

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

#include "mkexfat.h"
#include <exfat.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

static int logarithm2(int n)
{
	size_t i;

	for (i = 0; i < sizeof(int) * CHAR_BIT - 1; i++)
		if ((1 << i) == n)
			return i;
	return -1;
}

static void usage(const char* prog)
{
	fprintf(stderr, "Usage: %s [-i volume-id] [-n label] "
			"[-p partition-first-sector] "
			"[-s sectors-per-cluster] [-V] <device>\n", prog);
	exit(1);
}

int main(int argc, char* argv[])
{
	const char* spec = NULL;
	int opt;
	int spc_bits = -1;
	const char* volume_label = NULL;
	uint32_t volume_serial = 0;
	uint64_t first_sector = 0;
	struct exfat_dev* dev;

	printf("mkexfatfs %s\n", VERSION);

	while ((opt = getopt(argc, argv, "i:n:p:s:V")) != -1)
	{
		switch (opt)
		{
		case 'i':
			volume_serial = strtol(optarg, NULL, 16);
			break;
		case 'n':
			volume_label = optarg;
			break;
		case 'p':
			first_sector = strtoll(optarg, NULL, 10);
			break;
		case 's':
			spc_bits = logarithm2(atoi(optarg));
			if (spc_bits < 0)
			{
				exfat_error("invalid option value: '%s'", optarg);
				return 1;
			}
			break;
		case 'V':
			puts("Copyright (C) 2011-2023  Andrew Nayenko");
			return 0;
		default:
			usage(argv[0]);
			break;
		}
	}
	if (argc - optind != 1)
		usage(argv[0]);
	spec = argv[optind];

	dev = exfat_open(spec, EXFAT_MODE_RW);
	if (dev == NULL)
		return 1;
	if (exfat_mkfs(dev, 9, spc_bits, volume_label, volume_serial,
				first_sector) != 0)
	{
		exfat_close(dev);
		return 1;
	}
	if (exfat_close(dev) != 0)
		return 1;
	printf("File system created successfully.\n");
	return 0;
}
