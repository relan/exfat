/*
	main.c (30.01.20)
	Resize exFAT volume

	Free exFAT implementation.
	Copyright (C) 2011-2018  Andrew Nayenko
	          (C) 2020  Tsuyoshi HASEGAWA

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
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define exfat_debug(format, ...)	// MEMO: disable debug display

#include "ope.h"

#include "method_0.c"
#include "method_1.c"
#include "method_2.c"

static int do_resize(struct exfat_dev *dev, off_t fssize)
{
	const int (*methods[])(struct resizeinfo *) = {
		resize_method_0,
		resize_method_1,
		resize_method_2,
		NULL
	};

	struct resizeinfo *ri = init_resizeinfo(dev,fssize);
	if (ri==NULL) return 1;
	if (ri->secdata == NULL) {
		free_resizeinfo(ri);
		return 0;
	}

	int r = -1;
	for(int i=0; methods[i]!=NULL && r!=0; ++i) {
		r = methods[i](ri);
		if (r>0) {
			free_resizeinfo(ri);
			return 1;
		}
	}
	if (r < 0) {
		exfat_error("Don't know resize method from the current state");
		free_resizeinfo(ri);
		return 1; 
	}

	// Commit to file system
	r = commit_resizeinfo(ri);
	free_resizeinfo(ri);

	if (r == 0) printf("File system resized successfully.\n");
	return r;
}

static int resize(const char *spec, off_t size_user_defined)
{
	struct exfat_dev *dev;
	struct exfat ef;
	off_t fssize;
	
	if (exfat_mount(&ef,spec,"ro"))
	{
		exfat_error("Failed to mount as exfat '%s'", spec);
		return 1;
	}
	exfat_unmount(&ef);
	exfat_debug("mount ok");

	dev = exfat_open(spec,EXFAT_MODE_RW);
	if (dev==NULL)
		return 1;

	// Check partition size
	fssize = exfat_get_size(dev);
	if (size_user_defined != 0) {
		if (fssize < size_user_defined) {
			exfat_error("specified size is too large");
			return 1;
		}
		fssize = size_user_defined;
	}
	exfat_debug("partition size: %ld bytes",fssize);

	if (do_resize(dev,fssize)) {
		exfat_error("Process error occured");
		exfat_close(dev);
		return 1;
	}

	exfat_close(dev);
	return 0;
}


static void usage(const char* prog)
{
	fprintf(stderr, "Usage: %s [-V] <device>\n", prog);
	exit(1);
}

int main(int argc, char* argv[])
{
	int opt;
	const char* spec = NULL;

	printf("resizeexfatfs %s\n", VERSION);

	while ((opt = getopt(argc, argv, "V")) != -1)
	{
		switch (opt)
		{
		case 'V':
			puts("Copyright (C) 2011-2018  Andrew Nayenko");
			puts("          (C) 2020    Tsuyoshi HASEGAWA");
			return 0;
		default:
			usage(argv[0]);
		}
	}
	if (argc - optind != 1)
		usage(argv[0]);
	spec = argv[optind];

	return resize(spec,0);
}

