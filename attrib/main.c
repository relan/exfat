/*
	main.c (20.01.11)
	Prints or changes exFAT file attributes

	Free exFAT implementation.
	Copyright (C) 2020       Endless OS Foundation LLC

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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void usage(const char* prog)
{
	fprintf(stderr,
		"Display current attributes:\n"
		"  %1$s <device> <file>\n"
		"\n"
		"Set attributes:\n"
		"  %1$s [FLAGS] <device> <file>\n"
		"\n"
		"Flags:\n"
		"  -r    Set read-only flag\n"
		"  -R    Clear read-only flag\n"
		"  -i    Set hidden flag\n"
		"  -I    Clear hidden flag\n"
		"  -s    Set system flag\n"
		"  -S    Clear system flag\n"
		"  -a    Set archive flag\n"
		"  -A    Clear archive flag\n"
		"\n"
		"  -h    Display this help message\n"
		"  -V    Display version information\n",
		prog);
	exit(1);
}

static void print_attribute(uint16_t attribs, uint16_t attrib, const char *label)
{
	printf("%9s: %s\n", label, (attribs & attrib) ? "yes" : "no");
}

int main(int argc, char* argv[])
{
	int opt;
	int ret;
	const char* spec = NULL;
	const char* options = "";
	const char* file_path = NULL;
	struct exfat ef;
	struct exfat_node* node;
	uint16_t add_flags = 0;
	uint16_t clear_flags = 0;

	while ((opt = getopt(argc, argv, "rRiIsSaAhV")) != -1)
	{
		switch (opt)
		{
		case 'V':
			printf("exfatattrib %s\n", VERSION);
			puts("Copyright (C) 2011-2018  Andrew Nayenko");
			puts("Copyright (C) 2020       Endless OS Foundation LLC");
			return 0;
		case 'r':
			add_flags |= EXFAT_ATTRIB_RO;
			break;
		case 'R':
			clear_flags |= EXFAT_ATTRIB_RO;
			break;
		/* "-h[elp]" is taken; i is the second letter of "hidden" and
		 * its synonym "invisible"
		 */
		case 'i':
			add_flags |= EXFAT_ATTRIB_HIDDEN;
			break;
		case 'I':
			clear_flags |= EXFAT_ATTRIB_HIDDEN;
			break;
		case 's':
			add_flags |= EXFAT_ATTRIB_SYSTEM;
			break;
		case 'S':
			clear_flags |= EXFAT_ATTRIB_SYSTEM;
			break;
		case 'a':
			add_flags |= EXFAT_ATTRIB_ARCH;
			break;
		case 'A':
			clear_flags |= EXFAT_ATTRIB_ARCH;
			break;
		default:
			usage(argv[0]);
		}
	}

	if ((add_flags & clear_flags) != 0)
	{
		exfat_error("Can't set and clear the same flag");
		exit(1);
	}

	if (argc - optind != 2)
		usage(argv[0]);

	spec = argv[optind];
	file_path = argv[optind + 1];

	if ((add_flags | clear_flags) == 0)
		options = "ro";

	if ((ret = exfat_mount(&ef, spec, options)) != 0)
	{
		exfat_error("Failed to mount %s: %s", spec, strerror(-ret));
		return 1;
	}

	if ((ret = exfat_lookup(&ef, &node, file_path)) != 0)
	{
		exfat_error("Failed to look up %s: %s", file_path, strerror(-ret));
		return 1;
	}

	if ((add_flags | clear_flags) != 0)
	{
		uint16_t attrib = node->attrib;

		attrib |= add_flags;
		attrib &= ~clear_flags;

		if (node->attrib != attrib)
		{
			node->attrib = attrib;
			node->is_dirty = true;

			if ((ret = exfat_flush_node(&ef, node)) != 0)
			{
				exfat_error("Failed to flush changes to %s: %s", file_path, strerror(-ret));
				return 1;
			}
		}
	}
	else
	{
		print_attribute(node->attrib, EXFAT_ATTRIB_RO, "Read-only");
		print_attribute(node->attrib, EXFAT_ATTRIB_HIDDEN, "Hidden");
		print_attribute(node->attrib, EXFAT_ATTRIB_SYSTEM, "System");
		print_attribute(node->attrib, EXFAT_ATTRIB_ARCH, "Archive");
		/* Read-only attributes */
		print_attribute(node->attrib, EXFAT_ATTRIB_VOLUME, "Volume");
		print_attribute(node->attrib, EXFAT_ATTRIB_DIR, "Directory");
	}

	exfat_put_node(&ef, node);
	exfat_unmount(&ef);
	return 0;
}
