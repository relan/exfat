/*
	rootdir.c (09.11.10)
	Root directory creation code.

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

#include <unistd.h>
#include <errno.h>
#include "mkexfat.h"

off_t rootdir_alignment(void)
{
	return CLUSTER_SIZE(sb);
}

off_t rootdir_size(void)
{
	return CLUSTER_SIZE(sb);
}

int rootdir_write(off_t base, int fd)
{
	const struct exfat_entry eod_entry = {EXFAT_ENTRY_EOD};

	if (write(fd, &label_entry, sizeof(struct exfat_entry)) == -1)
		return 1;
	if (write(fd, &bitmap_entry, sizeof(struct exfat_entry)) == -1)
		return 1;
	if (write(fd, &upcase_entry, sizeof(struct exfat_entry)) == -1)
		return 1;
	if (write(fd, &eod_entry, sizeof(struct exfat_entry)) == -1)
		return 1;

	sb.rootdir_cluster = cpu_to_le32(OFFSET_TO_CLUSTER(base));
	return 0;
}
