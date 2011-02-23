/*
	mkexfat.h (09.11.10)
	Common declarations.

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

#ifndef MKFS_MKEXFAT_H_INCLUDED
#define MKFS_MKEXFAT_H_INCLUDED

#include <exfat.h>

extern struct exfat_super_block sb;
extern struct exfat_entry_label label_entry;
extern struct exfat_entry_bitmap bitmap_entry;
extern struct exfat_entry_upcase upcase_entry;

#define OFFSET_TO_SECTOR(off) ((off) >> (sb).sector_bits)
#define SECTOR_TO_CLUSTER(sector) \
	((((sector) - le32_to_cpu((sb).cluster_sector_start)) >> (sb).spc_bits) + \
		EXFAT_FIRST_DATA_CLUSTER)
#define OFFSET_TO_CLUSTER(off) SECTOR_TO_CLUSTER(OFFSET_TO_SECTOR(off))

#endif /* ifndef MKFS_MKEXFAT_H_INCLUDED */
