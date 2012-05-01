/*
	cbm.h (09.11.10)
	Clusters Bitmap creation code.

	Copyright (C) 2011, 2012  Andrew Nayenko

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

#ifndef MKFS_CBM_H_INCLUDED
#define MKFS_CBM_H_INCLUDED

off_t cbm_alignment(void);
off_t cbm_size(void);
int cbm_write(struct exfat_dev* dev, off_t base);

#endif /* ifndef MKFS_CBM_H_INCLUDED */
