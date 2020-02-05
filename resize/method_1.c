/*
	method_1.c (30.01.20)
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

static int change_fileentry_startcluster(struct resizeinfo *ri);
static int move_datacluster_o2n(struct resizeinfo *ri, cluster_t clu);

//
// Methods to simply move the entire data clusters
// (obsolete)
//
static int resize_method_1(struct resizeinfo *ri)
{
	return -1; // don't use

    // same cluster size?
    if (ri->sb.spc_bits != ri->osb.spc_bits)
        return -1;

	exfat_debug(__FUNCTION__);

	if (expand_systemArea(ri)) return 1;

	// move data cluster to new allocated cluster
	change_fileentry_startcluster(ri);
	off_t movedc = 0;
	off_t clucnt = le32_to_cpu(ri->sb.cluster_count);
	for (off_t i = clucnt-1; i >= EXFAT_FIRST_DATA_CLUSTER; --i) {
		/*if ((clucnt-i) % 5 == 0)*/ printf(" %ld/%ld\r",clucnt-i,clucnt);
		if (bmpget(ri,i)) {
			if (move_datacluster_o2n(ri,i)) {
				exfat_error("Failed to move data cluster");
				exfat_error("The file system may have been corrupted..");
				return 1;
			}
			++movedc;
		}
	}
	printf("\e[2KMoved %ld cluster(s)\n",movedc);

	{
		off_t clusize = CLUSTER_SIZE(ri->sb);
		cluster_t bmpcno  = le32_to_cpu(ri->bmp->start_cluster);
		cluster_t upccno  = le32_to_cpu(ri->upc->start_cluster);

		// set FAT and set "used" to cluster bitmap
		fatset(ri,bmpcno,bmpcno+(ri->bmpsize/clusize)-1);
		fatset(ri,upccno,upccno+(ri->upcsize/clusize)-1);
		bmpset(ri,bmpcno,upccno+(ri->upcsize/clusize)-1,1);
	}

	return 0;
}

static int move_datacluster_o2n(struct resizeinfo *ri, cluster_t clu)
{
	assert(ri->sb.spc_bits == ri->osb.spc_bits);

	off_t rootofs = ROOTOFS(ri);
	if (rootofs == 0) return 0;

	if (ri->writeenable) {
		off_t clusize = CLUSTER_SIZE(ri->sb);
		off_t fo = c2o(&ri->osb,clu);
		off_t to = c2o(&ri->sb,clu+rootofs);
		if (fo!=to) {
			if (exfat_pread( ri->dev,ri->cludata,clusize,fo) < 0) return 1;
			if (exfat_pwrite(ri->dev,ri->cludata,clusize,to) < 0) return 1;
		}
	}
	else {
		off_t fs = c2s(&ri->osb,clu);
		off_t ts = c2s(&ri->sb,clu+rootofs);
		//exfat_debug("clusterMove: sec %ld -> sec %ld (%ld)",fs,ts,rootofs);
	}

	cluster_t fat = fatget(ri,clu);
	if (fat != EXFAT_CLUSTER_END && 
		fat != EXFAT_CLUSTER_BAD && 
		fat != EXFAT_CLUSTER_FREE ) fat = fat+rootofs;

	fatset_s(ri,clu,EXFAT_CLUSTER_FREE);
	fatset_s(ri,clu+rootofs,fat);

	bmpset_s(ri,clu,0);
	bmpset_s(ri,clu+rootofs,1);

	return 0;
}

static int change_fileentry_startcluster(struct resizeinfo *ri)
{
	char tmpbuf[1024]; tmpbuf[0] = '\0';

	if (ROOTOFS(ri) == 0) return 0;

	for (struct dirinfo *di = ri->rootdir; di != NULL; di = di->next) {
		for(struct exfat_entry *ents = di->data; ents->type; ++ents) {
			switch(ents->type) {
				case EXFAT_ENTRY_FILE_NAME:
				if (false) {
					struct exfat_entry_name *name = (struct exfat_entry_name *)ents;
					char tmpbuf1[256];
					get_fileent_name(tmpbuf1,name->name,sizeof(tmpbuf1));
					strncat(tmpbuf,tmpbuf1,sizeof(tmpbuf)-1);
					if ((name+1)->type != EXFAT_ENTRY_FILE_NAME) {
							exfat_debug(" * %s",tmpbuf);
							tmpbuf[0] = '\0';
					}
				}
				break;

				case EXFAT_ENTRY_FILE_INFO:
				{
					struct exfat_entry_meta1 *info1 = (struct exfat_entry_meta1 *)ents-1;
					struct exfat_entry_meta2 *info2 = (struct exfat_entry_meta2 *)ents;
					if (info1->type == EXFAT_ENTRY_FILE) {
						cluster_t headcluno = le32_to_cpu(info2->start_cluster);
						info2->start_cluster = cpu_to_le32(headcluno+ROOTOFS(ri));
						info1->checksum = cpu_to_le16(0);
						info1->checksum = exfat_calc_checksum((struct exfat_entry *)info1,info1->continuations+1);
					}
				}
				break;

				default: break;
			}
		}
		di->head_cluster += ROOTOFS(ri);
	}

	return 0;
}

