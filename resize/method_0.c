/*
	method_0.c (14.01.20)
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
#include "ope.h"

/*
   Most fast method, but only mount under UNIX systems.
   can't mount under Windows...
*/
int resize_method_0(struct resizeinfo *ri)
{
	off_t secsize, clusize;
	off_t fatcount, fathead, fattail, fatcno, fatsno;	
	off_t bmpsize, bmpsizeR, bmpcno;
	void *p;

	return -1;  /* don't use */

    /* same culster size? */
	if (ri->sb.spc_bits != ri->osb.spc_bits)
        return -1;

	exfat_debug(__FUNCTION__);

	/* Get parameters from superblock */
	secsize = SECTOR_SIZE(ri->sb);
	clusize = CLUSTER_SIZE(ri->sb);

	/* allocate FAT */
	fatcount = le32_to_cpu(ri->sb.fat_sector_count);
	p = malloc(fatcount*secsize);
	if (p==NULL)
	{
		exfat_error("failed to allocate memory");
		return 1;
	}
	memset(p,0,fatcount*secsize);
	memcpy(p,ri->fatdata,ri->fatsize);
	free(ri->fatdata);
	ri->fatdata = p;
	ri->fatsize = fatcount*secsize;

	/* allocate cluster bitmap */
	fathead  = le32_to_cpu(ri->sb.fat_sector_start);
	fattail  = fathead + fatcount;
	fathead *= secsize;
	fattail *= secsize;
	bmpsize  = DIV_ROUND_UP((fattail-fathead)/sizeof(cluster_t),8);
	bmpsizeR = ROUND_UP(bmpsize,clusize);
	bmpcno   = le32_to_cpu(ri->sb.cluster_count)-(bmpsizeR/clusize);
	p = malloc(bmpsizeR);
	if (p==NULL)
	{
		exfat_error("failed to allocate memory");
		return 1;
	}
	memset(p,0,bmpsizeR);
	memcpy(p,ri->bmpdata,ri->bmpsize);
	free(ri->bmpdata);
	ri->bmpdata = p;
	ri->bmpsize = bmpsizeR;
	ri->bmpoffs = c2o(&ri->sb,bmpcno);
	ri->bmp->start_cluster = cpu_to_le32(bmpcno);
	ri->bmp->size = cpu_to_le64(bmpsize);
    fatset(ri,bmpcno,bmpcno+(bmpsizeR/clusize)-1);
	exfat_debug("expand bmpsize: %ld",bmpsize);

	/* set new FAT position */
	fatcno = bmpcno - DIV_ROUND_UP(ri->fatsize,clusize);
	fatsno = c2s(&ri->sb,fatcno) & (off_t)(~128);
	ri->sb.fat_sector_start = cpu_to_le32(fatsno);
	ri->fatoffs = fatsno * secsize;

	/* set "used" to cluster bitmap */
	bmpset(ri,s2c(&ri->sb,fatsno),le32_to_cpu(ri->sb.cluster_count)-1,1);

	return 0;
}

