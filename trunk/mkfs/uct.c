/*
	uct.c (09.11.10)
	Upper Case Table creation code.

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
#include "uctc.h"

off_t uct_alignment(void)
{
	return CLUSTER_SIZE(sb);
}

off_t uct_size(void)
{
	return sizeof(upcase_table);
}

static le32_t uct_checksum(void)
{
	size_t i;
	uint32_t sum = 0;

	for (i = 0; i < sizeof(upcase_table); i++)
		sum = ((sum << 31) | (sum >> 1)) + upcase_table[i];
	return cpu_to_le32(sum);
}

int uct_write(off_t base, int fd)
{
	if (write(fd, upcase_table, sizeof(upcase_table)) == -1)
		return errno;
	upcase_entry.checksum = uct_checksum();
	upcase_entry.start_cluster = cpu_to_le32(OFFSET_TO_CLUSTER(base));
	upcase_entry.size = cpu_to_le64(sizeof(upcase_table));
	return 0;
}
