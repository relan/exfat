/*
	method_2.c (30.01.20)
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

static int remap_directory_entries(struct resizeinfo *ri);

/*
   Methods that move only data clusters that need to be moved
*/
int resize_method_2(struct resizeinfo *ri)
{
	off_t clusize;
	cluster_t bmpcno, upccno, rootcno;

    /* same cluster size? */
    if (ri->sb.spc_bits != ri->osb.spc_bits)
        return -1;

	exfat_debug(__FUNCTION__);

	if (expand_systemArea(ri))
		return 1;
	if (remap_directory_entries(ri))
		return 1;

	clusize = CLUSTER_SIZE(ri->sb);
	bmpcno  = le32_to_cpu(ri->bmp->start_cluster);
	upccno  = le32_to_cpu(ri->upc->start_cluster);
	rootcno = ri->rootdir->head_cluster;

	/* set FAT and set "used" to cluster bitmap */
	fatset(ri,bmpcno,bmpcno+(ri->bmpsize/clusize)-1);
	fatset(ri,upccno,upccno+(ri->upcsize/clusize)-1);
	fatset(ri,rootcno,rootcno+(ri->rootdir->size/clusize)-1);
	bmpset(ri,bmpcno,rootcno+(ri->rootdir->size/clusize)-1,1);

	return 0;
}


static cluster_t REMAP(struct resizeinfo *ri, cluster_t cluno)
{
	off_t old;

	if (cluno==EXFAT_CLUSTER_END || cluno==EXFAT_CLUSTER_FREE)
		return cluno;

	old = c2o(&ri->osb,cluno);
	return s2c(&ri->sb,(old >> ri->sb.sector_bits));
}

static bool isNeedMoved(struct resizeinfo *ri, struct exfat_entry *ent)
{
	struct exfat_entry_meta2 *info2 = (struct exfat_entry_meta2 *)ent;

	cluster_t cluno = le32_to_cpu(info2->start_cluster);
	off_t clusize = CLUSTER_SIZE(ri->sb);
	off_t headofs = c2o(&ri->sb,le32_to_cpu(ri->sb.rootdir_cluster)+(ri->rootdir->size/clusize)-1);

	fatnext_first(ri,(info2->flags & EXFAT_FLAG_CONTIGUOUS)!=0,DIV_ROUND_UP(le64_to_cpu(info2->size),clusize));
	do {
		if (c2o(&ri->osb,cluno) <= headofs)
			return true;
		cluno = fatnext(ri,cluno);		
	} while(cluno!=EXFAT_CLUSTER_END);

	return false;
}

static cluster_t allocate_datacluster(struct resizeinfo *ri)
{
	cluster_t count = le32_to_cpu(ri->sb.cluster_count);
	for (cluster_t i = EXFAT_FIRST_DATA_CLUSTER; i < count; ++i ) {
		if (!bmpget(ri,i))
		{
			bmpset_s(ri,i,1);
			return i;
		}
	}
	return 0;
}

static bool remap_dataclusters(struct resizeinfo *ri, struct exfat_entry *ent)
{
	struct exfat_entry_meta1 *info1 = (struct exfat_entry_meta1 *)ent-1;
	struct exfat_entry_meta2 *info2 = (struct exfat_entry_meta2 *)ent;

	if (info1->type != EXFAT_ENTRY_FILE)
		return false;

	off_t clusize = CLUSTER_SIZE(ri->sb);
	fatnext_first(ri,(info2->flags & EXFAT_FLAG_CONTIGUOUS)!=0,DIV_ROUND_UP(le64_to_cpu(info2->size),clusize));

	if (isNeedMoved(ri,ent))
	{
		/* Move data cluster */
		cluster_t orgc = le32_to_cpu(info2->start_cluster);
		cluster_t prev = 0;
		do {
			cluster_t cluno = allocate_datacluster(ri);
			if (cluno==0)
				return false;
			if (prev==0)
				info2->start_cluster = cpu_to_le32(cluno);
			else
				fatset_s(ri,prev,cluno);

			if (ri->writeenable)
			{
				off_t fo = c2o(&ri->osb,orgc);
				off_t to = c2o(&ri->sb,cluno);
				if (fo!=to)
				{
					if (exfat_pread( ri->dev,ri->cludata,clusize,fo) < 0)
						return false;
					if (exfat_pwrite(ri->dev,ri->cludata,clusize,to) < 0)
						return false;
				}
			}
			prev = cluno;
			orgc = fatnext(ri,orgc);
		} while(orgc!=EXFAT_CLUSTER_END);
		fatset_s(ri,prev,EXFAT_CLUSTER_END);

		info2->flags &= ~EXFAT_FLAG_CONTIGUOUS;
	}
	else
	{
		/* Remap data cluster */
		cluster_t cluno = le32_to_cpu(info2->start_cluster);
		cluster_t rclun = REMAP(ri,cluno);
		cluster_t nclun;
		info2->start_cluster = cpu_to_le32(rclun);
		do {
			rclun = REMAP(ri,cluno);
			nclun = fatnext(ri,cluno);		

			fatset_s(ri,cluno,EXFAT_CLUSTER_FREE);
			bmpset_s(ri,cluno,0);

			fatset_s(ri,rclun,REMAP(ri,nclun));
			bmpset_s(ri,rclun,1);

			cluno = nclun;
		} while(cluno!=EXFAT_CLUSTER_END);
	}

	/* calculate file entry checksum */
	info1->checksum = cpu_to_le16(0);
	info1->checksum = exfat_calc_checksum((struct exfat_entry *)info1,info1->continuations+1);
	return true;
}

static int remap_directory_entries(struct resizeinfo *ri)
{
	ri->rootdir->head_cluster = le32_to_cpu(ri->sb.rootdir_cluster);

	for(struct dirinfo *di = ri->rootdir; di != NULL; di = di->next)
	{
		for(struct exfat_entry *ent = di->data; ent->type; ++ent)
		{
			if (ent->type == EXFAT_ENTRY_FILE_INFO)
			{
				if (!remap_dataclusters(ri,ent))
				{
					exfat_error("Failed to file move/remap");
					exfat_error("The file system may have been corrupted..");
					return 1;
				}
			}
		}
		if (di != ri->rootdir)
		{
			di->head_cluster = REMAP(ri,di->head_cluster);
		}
	}
	return 0;
}

