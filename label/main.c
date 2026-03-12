/*
	main.c (20.01.11)
	Prints or changes exFAT volume label.

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

#include <exfat.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[])
{
	struct exfat ef;
	int rc = 0;
	const char* device = NULL;
	const char* label = NULL;
	const char* const usage = "Usage: %s [-V] <device> [label]\n";
	int c;

	while ((c = getopt(argc, argv, "V")) != -1)
	{
		if (c == 'V')
		{
			printf("exfatlabel %s\n", VERSION);
			puts("Copyright (C) 2011-2023  Andrew Nayenko");
			return 0;
		}

		fprintf(stderr, usage, argv[0]);
		return 1;
	}

	if (optind < argc)
		device = argv[optind++];

	if (optind < argc)
		label = argv[optind++];

	if (!device || optind < argc)
	{
		fprintf(stderr, usage, argv[0]);
		return 1;
	}

	if (label)
	{
		if (exfat_mount(&ef, device, "") != 0)
			return 1;
		rc = (exfat_set_label(&ef, label) != 0);
	}
	else
	{
		if (exfat_mount(&ef, device, "ro") != 0)
			return 1;
		puts(exfat_get_label(&ef));
	}

	exfat_unmount(&ef);
	return rc;
}
