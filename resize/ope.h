/*
	ope.h (30.01.20)
	Resize exFAT volume

	Free exFAT implementation.
	Copyright (C) 2011-2018  Andrew Nayenko
	          (C) 2020    Tsuyoshi HASEGAWA

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
#ifndef OPE_H__
#define OPE_H__

#include <exfat.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define exfat_debug(format, ...)

struct dirinfo {
	struct dirinfo *prev;
	struct dirinfo *next;

	cluster_t head_cluster;
	void *data;
	off_t size;
	bool  is_contiguous;
};

struct resizeinfo
{
	struct exfat_dev *dev;
	struct exfat_super_block sb,osb;

	off_t dirs;
	off_t files;

	void *secdata;
	void *cludata;
	bool  writeenable;

	void *fatdata;
	off_t fatoffs,fatsize;
	bool  is_contiguous;
	off_t linkcount;

	struct exfat_entry_bitmap *bmp;
	void *bmpdata;
	off_t bmpoffs,bmpsize;

	struct exfat_entry_upcase *upc;
	void *upcdata;
	off_t upcoffs,upcsize;

	struct dirinfo *rootdir;
	struct dirinfo *dirtail;
};

static off_t ROOTOFS(struct resizeinfo *ri)
{
	return le32_to_cpu(ri->sb.rootdir_cluster) - le32_to_cpu(ri->osb.rootdir_cluster);
}

static void dbgdump(const char *caption,const void *buf,off_t len)
{
	if (false) {
		const unsigned char *p = (const unsigned char *)buf;
		printf("\n%s %ld\n",caption,len);
		for (off_t i = 0; i < ((len>128) ? 128 : len); ++i) {
			printf(" %02X",p[i]);
			if (i%16 == 15) printf("\n");
		}
		printf("\n");
	}
}

/*
 * Sector to absolute offset.
 */
static off_t s2o(const struct exfat_super_block* sb, off_t sector)
{
	return sector << sb->sector_bits;
}

/*
 * Cluster to sector.
 */
static off_t c2s(const struct exfat_super_block* sb, cluster_t cluster)
{
	if (cluster < EXFAT_FIRST_DATA_CLUSTER)
		exfat_bug("invalid cluster number %u", cluster);
	return le32_to_cpu(sb->cluster_sector_start) +
		((off_t) (cluster - EXFAT_FIRST_DATA_CLUSTER) << sb->spc_bits);
}

/*
 * Cluster to absolute offset.
 */
static off_t c2o(const struct exfat_super_block* sb, cluster_t cluster)
{
	return s2o(sb, c2s(sb, cluster));
}

/*
 * Sector to cluster.
 */
static cluster_t s2c(const struct exfat_super_block* sb, off_t sector)
{
	return ((sector - le32_to_cpu(sb->cluster_sector_start)) >>
			sb->spc_bits) + EXFAT_FIRST_DATA_CLUSTER;
}

/*
 * Size in bytes to size in clusters (rounded upwards).
 */
static uint32_t bytes2clusters(const struct exfat_super_block* sb, uint64_t bytes)
{
	uint64_t cluster_size = CLUSTER_SIZE(*sb);
	return DIV_ROUND_UP(bytes, cluster_size);
}

static void bmpset_s(struct resizeinfo *ri, cluster_t clusno, int used)
{
	unsigned char *bmp;
	unsigned char f;

	if (clusno < 2)
		return;
	clusno -= 2;

	bmp = ri->bmpdata;
	f = 1 << (clusno % 8);
	if (used & 1)
		bmp[clusno/8] |= f;
	else
		bmp[clusno/8] &= (0xFF^f);
}

static bool bmpget(struct resizeinfo *ri, cluster_t clusno)
{
	unsigned char *bmp;
	unsigned char f;

	if (clusno < 2)
		return false;
	clusno -= 2;

	bmp = ri->bmpdata;
	f = 1 << (clusno % 8);
	return (bmp[clusno/8] & f)!=0;
}

static off_t bmpget_aloccnt(struct resizeinfo *ri)
{
	off_t cnt = 0;
	off_t clucnt = le32_to_cpu(ri->sb.cluster_count);
	for (off_t i = clucnt-1; i >= EXFAT_FIRST_DATA_CLUSTER; --i )
	{
		if (bmpget(ri,i))
			++cnt;
	}
	return cnt;	
}

static void bmpset(struct resizeinfo *ri, cluster_t start, cluster_t end, int f)
{
	for (off_t i = start; i <= end; ++i)
		bmpset_s(ri,i,f);
}


static void fatset_s(struct resizeinfo *ri, cluster_t clusno, cluster_t n)
{
	cluster_t *fat = ri->fatdata;
	fat[clusno] = n;
}

static cluster_t fatget(struct resizeinfo *ri, cluster_t clusno)
{
	cluster_t *fat = ri->fatdata;
	return fat[clusno];
}

static void fatend(struct resizeinfo *ri, cluster_t clusno)
{
	fatset_s(ri,clusno,EXFAT_CLUSTER_END);
}

static void fatset(struct resizeinfo *ri, cluster_t start, cluster_t end)
{
	cluster_t i;
	for (i=start; i < end; ++i) fatset_s(ri,i,i+1);
	fatend(ri,end);
}

static void fatnext_first(struct resizeinfo *ri, bool cont, off_t count)
{
	ri->is_contiguous = cont;
	ri->linkcount = cont ? count : 0;
}

static cluster_t fatnext(struct resizeinfo *ri, cluster_t cluno)
{
	if (ri->is_contiguous)
	{
		if (ri->linkcount!=0)
			ri->linkcount--;
		return (ri->linkcount==0) ? EXFAT_CLUSTER_END : (cluno+1);
	}
	else
	{
		cluster_t next = fatget(ri,cluno);	
		if (next == EXFAT_CLUSTER_FREE || next == EXFAT_CLUSTER_BAD)
			next = EXFAT_CLUSTER_END;
		return next;
	}
}

static off_t fatlinks(struct resizeinfo *ri, cluster_t headcluno)
{
	off_t cnt = 0;
	for(cluster_t cur = headcluno; cur != EXFAT_CLUSTER_END; cur = fatnext(ri,cur), ++cnt );
	/* exfat_debug("fatlinks: %ld",cnt); */
	return cnt;
}


static void get_fileent_name(char *out, le16_t name[15], size_t outsize)
{
	le16_t tmpbuf[16];
	for (int i=0; i <= 14; ++i)
	{
		tmpbuf[i] = name[i];
		tmpbuf[i+1] = cpu_to_le16(0);
	};
	utf16_to_utf8(out,tmpbuf,outsize,utf16_length(tmpbuf));
}

static int commit_superblock(struct resizeinfo *ri)
{
	struct exfat_dev* dev = ri->dev;
	struct exfat_super_block *sb = &ri->sb;
	void *secdata = ri->secdata;
	off_t secsize = SECTOR_SIZE(*sb);

	uint32_t vbr_checksum;
	int	i,j;

	sb->allocated_percent = 100 * bmpget_aloccnt(ri) / le32_to_cpu(sb->cluster_count);

	/* Write SuperBlock */
	if (exfat_pwrite(dev, sb, sizeof(struct exfat_super_block), 0) < 0)
	{
		exfat_error("failed to write super block");
		return 1;
	}

	/* Write Checksum */
	for (i = 0; i < 11; i++)
	{
		if (exfat_pread(dev, secdata, secsize, i*secsize) < 0)
		{
			exfat_error("failed to read VBR sector");
			return 1;
		}
		if (i==0)
			vbr_checksum = exfat_vbr_start_checksum(secdata, secsize);
		else
			vbr_checksum = exfat_vbr_add_checksum(secdata, secsize, vbr_checksum);
	}
	for (j = 0; j < secsize / sizeof(vbr_checksum); j++)
		((le32_t*)secdata)[j] = cpu_to_le32(vbr_checksum);

	if (exfat_pwrite(dev, secdata, secsize, i*secsize) < 0)
	{
		exfat_error("failed to write checksum sector");
		return 1;
	}

	/* Backup */
	for (i = 0; i < 12; i++)
	{
		if (exfat_pread(dev, secdata, secsize, i*secsize) < 0)
		{
			exfat_error("failed to read VBR sector");
			return 1;
		}
		if (exfat_pwrite(dev, secdata, secsize,(i+12)*secsize) < 0)
		{
			exfat_error("failed to write backup VBR sector");
			return 1;
		}
	}

	return 0;
}

static void read_rootdir_entries(struct resizeinfo *ri)
{
	for(struct exfat_entry *ents = ri->rootdir->data; ents->type; ++ents) {
		if (ri->bmp != NULL && ri->upc != NULL) return;
		switch(ents->type) {
			case EXFAT_ENTRY_BITMAP:
				ri->bmp = (struct exfat_entry_bitmap *)ents;
				break;

			case EXFAT_ENTRY_UPCASE:
				ri->upc = (struct exfat_entry_upcase *)ents;
				break;

			default:
				break;
		}
	}
}

static struct dirinfo *create_dirinfo(struct resizeinfo *ri, bool is_contiguous, cluster_t headcluno);
static struct dirinfo *free_dirinfo(struct dirinfo *di);
static int commit_rootdir(struct resizeinfo *ri);
static int read_directory_entries(struct resizeinfo *ri);

static int commit_resizeinfo(struct resizeinfo *ri)
{
	if (!ri->writeenable)
	{
		exfat_debug("commit done (ignored)");
		return 0;
	}

	exfat_debug("commit begin..");

	/* FAT */
	if (exfat_pwrite(ri->dev,ri->fatdata,ri->fatsize,ri->fatoffs) < 0)
	{
		exfat_error("FAT rewrite failed");
		return 1;
	}

	/* Cluster bitmap */
	if (exfat_pwrite(ri->dev,ri->bmpdata,ri->bmpsize,ri->bmpoffs) < 0)
	{
		exfat_error("Cluster bitmap rewrite failed");
		return 1;
	}

	/* Upcase table */
	if (exfat_pwrite(ri->dev,ri->upcdata,ri->upcsize,ri->upcoffs) < 0)
	{
		exfat_error("Upcase table rewrite failed");
		return 1;
	}

	/* Root directory */
	if (commit_rootdir(ri))
	{
		exfat_error("Root directory rewrite failed");
		return 1;
	}

	/* Superblock */
	if (commit_superblock(ri))
	{
		exfat_error("Superblock rewrite failed");
		return 1;
	}

	if (exfat_fsync(ri->dev))
	{
		exfat_error("fsync failed");
		return 1;
	}

	exfat_debug("commit done");
	return 0;
}

static void free_resizeinfo(struct resizeinfo *ri)
{
	for(struct dirinfo *p = ri->dirtail; p != NULL;)
		p = free_dirinfo(p);

	free(ri->upcdata);
	free(ri->bmpdata);
	free(ri->fatdata);
	free(ri->cludata);
	free(ri->secdata);
	free(ri);
}

static int calc_spcbits(int secbits, int orgspcbits, uint64_t volsize)
{
	uint64_t maxcluster = DIV_ROUND_UP(volsize,1 << (orgspcbits+secbits));
	exfat_debug("max cluster no. 0x%08lX",maxcluster);

	if (maxcluster > EXFAT_LAST_DATA_CLUSTER)
	{
		if (volsize < 256LL*1024*1024)		/* <256MB : 4KB */
			return MAX(0,12-secbits);

		if (volsize < 32LL*1024*1024*1024)	/* <32GB : 32KB */
			return MAX(0,15-secbits);

		for(int i=17;;++i)
		{
			if (DIV_ROUND_UP(volsize,1 << i) <= EXFAT_LAST_DATA_CLUSTER)
				return MAX(0,i-secbits);
		}
	}

	return orgspcbits;
}

static int setup_superblock(struct resizeinfo *ri, off_t volsize)
{
	struct exfat_super_block *sb = &ri->sb;

	sb->spc_bits = calc_spcbits(sb->sector_bits,sb->spc_bits,volsize);
	exfat_debug("spc_bits: %d (org: %d)",sb->spc_bits,ri->osb.spc_bits);
	if (sb->spc_bits != ri->osb.spc_bits)
	{
		exfat_warn("Because the cluster size after resizer is different,\n      "
				   "processing may not be possible or it may take a long time");
	}

	{
		off_t secsize = SECTOR_SIZE(*sb);
		off_t clusize = CLUSTER_SIZE(*sb);
	
		uint32_t clusters_max = volsize / clusize;
		uint32_t fat_sectors  = DIV_ROUND_UP(clusters_max * sizeof(cluster_t), secsize);

		uint64_t total_seccnt = volsize / secsize;
		uint32_t total_fatcnt = ROUND_UP(le32_to_cpu(sb->fat_sector_start) + fat_sectors, 1 << sb->spc_bits) - le32_to_cpu(sb->fat_sector_start);
		uint32_t total_clucnt = clusters_max - ((le32_to_cpu(sb->fat_sector_start) + total_fatcnt) >> sb->spc_bits);

		sb->sector_count     = cpu_to_le64(total_seccnt);
		sb->cluster_count    = cpu_to_le32(total_clucnt);
		sb->fat_sector_count = cpu_to_le32(total_fatcnt);
	}

	return 0;
}

static off_t check_badsector(struct resizeinfo *ri)
{
	off_t clucnt = le32_to_cpu(ri->sb.cluster_count);
	off_t badcnt = 0;
	for (off_t i = EXFAT_FIRST_DATA_CLUSTER; i < clucnt; ++i)
	{
		if (fatget(ri,i) == EXFAT_CLUSTER_BAD)
			++badcnt;
	}
	return badcnt;
}

static struct resizeinfo *init_resizeinfo(struct exfat_dev *dev, off_t volsize)
{
	off_t secsize,clusize,badsecs;

	struct resizeinfo *ri = malloc(sizeof(struct resizeinfo));
	if (ri==NULL)
	{
		exfat_error("failed to allocate memory");
		return NULL;
	}

	memset(ri,0,sizeof(struct resizeinfo));
	ri->dev = dev;
	ri->writeenable = true;

	/* Read superblock */
	if (exfat_pread(dev, &ri->sb, sizeof(struct exfat_super_block),0) < 0)
	{
		exfat_error("failed to read superblock");
		free_resizeinfo(ri);
		return NULL;
	}
	ri->osb = ri->sb;

	secsize = SECTOR_SIZE(ri->sb);
	clusize = CLUSTER_SIZE(ri->sb);

	if (le64_to_cpu(ri->sb.sector_count) >= volsize/secsize)
	{
		printf("Partition has been not extended, terminate without doing anything.\n");
		return ri; 
	}

	ri->secdata = malloc(secsize);
	if (ri->secdata == NULL)
	{
		exfat_error("failed to allocate memory");
		free_resizeinfo(ri);
		return NULL;
	}
	ri->cludata = malloc(clusize);
	if (ri->cludata == NULL)
	{
		exfat_error("failed to allocate memory");
		free_resizeinfo(ri);
		return NULL;
	}

	/* Read FAT */
	ri->fatoffs = s2o(&ri->sb,le32_to_cpu(ri->sb.fat_sector_start));
	ri->fatsize = le32_to_cpu(ri->sb.fat_sector_count) * secsize;
	ri->fatdata = malloc(ri->fatsize);
	if (ri->fatdata == NULL)
	{
		exfat_error("failed to allocate memory");
		free_resizeinfo(ri);
		return NULL;
	}
	if (exfat_pread(ri->dev,ri->fatdata,ri->fatsize,ri->fatoffs) < 0)
	{
		exfat_error("failed to read FAT");
		free_resizeinfo(ri);
		return NULL;
	}
	dbgdump("fat:",ri->fatdata,ri->fatsize);

	/* Check BAD sector */
	if ((badsecs = check_badsector(ri))!=0) {
		exfat_warn(
			"%ld bad sector(s) exists.\n      "
			"There may be problems with the resize process", badsecs);
	}

	/* Read root directory */
	ri->rootdir = ri->dirtail = create_dirinfo(ri,false,le32_to_cpu(ri->sb.rootdir_cluster));
	if (ri->rootdir == NULL)
	{
		exfat_error("failed to read root directory");
		free_resizeinfo(ri);
		return NULL;
	}
	dbgdump("\nrootdir:",ri->rootdir->data,ri->rootdir->size);
	read_rootdir_entries(ri);

	/* Read sub directories */
	if (read_directory_entries(ri))
	{
		free_resizeinfo(ri);
		return NULL;
	};
	printf("Exist %ld subdir(s), %ld file(s)\n", ri->dirs, ri->files);

	/* Read cluster bitmap */
	if (ri->bmp == NULL)
	{
		exfat_error("not found cluster bitmap");
		free_resizeinfo(ri);
		return NULL;
	}
	ri->bmpoffs = c2o(&ri->sb,le32_to_cpu(ri->bmp->start_cluster));
	ri->bmpsize = ROUND_UP(le64_to_cpu(ri->bmp->size),clusize);
	ri->bmpdata = malloc(ri->bmpsize);
	if (ri->bmpdata == NULL)
	{
		exfat_error("failed to allocate memory");
		free_resizeinfo(ri);
		return NULL;
	}
	exfat_pread(ri->dev,ri->bmpdata,ri->bmpsize,ri->bmpoffs);
	dbgdump("\ncluster bitmap:",ri->bmpdata,ri->bmpsize);

	/* Read upcase table */
	if (ri->upc == NULL)
	{
		exfat_error("not upcase table");
		free_resizeinfo(ri);
		return NULL;
	}
	ri->upcoffs = c2o(&ri->sb,le32_to_cpu(ri->upc->start_cluster));
	ri->upcsize = ROUND_UP(le64_to_cpu(ri->upc->size),clusize);
	ri->upcdata = malloc(ri->upcsize);
	if (ri->upcdata == NULL)
	{
		exfat_error("failed to allocate memory");
		free_resizeinfo(ri);
		return NULL;
	}
	exfat_pread(ri->dev,ri->upcdata,ri->upcsize,ri->upcoffs);
	dbgdump("\nupcase table:",ri->upcdata,ri->upcsize);

	/* Setup superblock for new volsize */
	setup_superblock(ri,volsize);

	return ri;
}

static struct dirinfo *create_dirinfo(struct resizeinfo *ri, bool is_contiguous, cluster_t headcluno)
{
	struct dirinfo *di = malloc(sizeof(struct dirinfo));
	if (di == NULL)
	{
		exfat_error("failed to allocate memory");
		return NULL;
	}

	exfat_debug("create_dirinfo (%p) %d",di,headcluno);
	off_t clusize = CLUSTER_SIZE(ri->sb);
	fatnext_first(ri, is_contiguous, 1);
	di->prev = NULL;
	di->next = NULL;
	di->size = fatlinks(ri,headcluno) * clusize;
	di->is_contiguous = is_contiguous;
	di->head_cluster = headcluno;
	di->data = malloc(di->size+sizeof(struct exfat_entry));
	if (di->data == NULL)
	{
		exfat_error("failed to allocate memory");
		free(di);
		return NULL;
	}

	void *p = di->data;
	memset(p+di->size,0,sizeof(struct exfat_entry));
	fatnext_first(ri, is_contiguous, 1);
	for(cluster_t cur = headcluno; cur != EXFAT_CLUSTER_END; cur = fatnext(ri,cur), p += clusize)
	{
		if (exfat_pread(ri->dev,p,clusize,c2o(&ri->sb,cur)) < 0)
		{
			exfat_error("failed to read directory");
			free(di->data);
			free(di);
			return NULL;
		}
	}

	di->prev = ri->dirtail;
	if (ri->dirtail) ri->dirtail->next = di;
	ri->dirtail = di;

	return di;
}

static int commit_dirinfo(struct resizeinfo *ri, struct dirinfo *di)
{
	exfat_debug("commit_dirinfo (%p) %d",di, di->head_cluster);

	off_t clusize = CLUSTER_SIZE(ri->sb);
	void *p = di->data;
	fatnext_first(ri,di->is_contiguous,DIV_ROUND_UP(di->size,clusize));
	for(cluster_t cur = di->head_cluster; cur != EXFAT_CLUSTER_END; cur = fatnext(ri,cur), p += clusize)
	{
		if (exfat_pwrite(ri->dev,p,clusize,c2o(&ri->sb,cur)) < 0)
			return 1;
		bmpset_s(ri,cur,1);
	}
	return 0;
}

static struct dirinfo *free_dirinfo(struct dirinfo *di)
{
	struct dirinfo *r = di->prev;
	if (r!=NULL) r->next = NULL;
	free(di->data);
	free(di);
	/* exfat_debug("  free_dirinfo (%p)",di); */
	return r;
}

static int commit_rootdir(struct resizeinfo *ri)
{
	for (struct dirinfo *p = ri->rootdir; p != NULL; p = p->next)
	{
		if (commit_dirinfo(ri,p))
			return 1;
	}
	return 0;
}

static int read_directory_entries(struct resizeinfo *ri)
{
	for (struct dirinfo *di = ri->rootdir; di != NULL; di = di->next)
	{
		for(struct exfat_entry *ents = di->data; ents->type; ++ents)
		{
			if (ents->type == EXFAT_ENTRY_FILE_INFO)
			{
				struct exfat_entry_meta1 *info1 = (struct exfat_entry_meta1 *)ents-1;
				struct exfat_entry_meta2 *info2 = (struct exfat_entry_meta2 *)ents;
				if (info1->type == EXFAT_ENTRY_FILE)
				{
					cluster_t headcluno = le32_to_cpu(info2->start_cluster);
					if (le16_to_cpu(info1->attrib) & EXFAT_ATTRIB_DIR) {
						if (create_dirinfo(ri,(info2->flags & EXFAT_FLAG_CONTIGUOUS)!=0,headcluno)==NULL)
							return 1;
						++ri->dirs;
					}
					else
						++ri->files;
				}
			}
		}
		di->head_cluster += ROOTOFS(ri);
 	}
	return 0;
}

static int expand_systemArea(struct resizeinfo *ri)
{
	off_t secsize = SECTOR_SIZE(ri->sb);
	off_t clusize = CLUSTER_SIZE(ri->sb);

	off_t fatcount,bmpsize,bmpsizeR;
	cluster_t fathead, fattail, bmpcno;
	void *p;

	/* allocate FAT */
	fatcount = le32_to_cpu(ri->sb.fat_sector_count);
	p = malloc(fatcount*secsize);
	if (p==NULL) {
		exfat_error("failed to allocate memory");
		return 1;
	}
	memset(p,0,fatcount*secsize);
	memcpy(p,ri->fatdata,ri->fatsize);
	free(ri->fatdata);
	ri->fatdata = p;
	ri->fatsize = fatcount*secsize;
	fathead = le32_to_cpu(ri->sb.fat_sector_start);
	fattail = fathead + fatcount;
	ri->sb.cluster_sector_start = cpu_to_le32(fattail);

	/* allocate cluster bitmap */
	fathead *= secsize;
	fattail *= secsize;
	bmpsize  = DIV_ROUND_UP((fattail-fathead)/sizeof(cluster_t),8);
	bmpsizeR = ROUND_UP(bmpsize,clusize);
	bmpcno   = EXFAT_FIRST_DATA_CLUSTER;
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

	/* allocate upcase table */
	off_t upccno = bmpcno+(bmpsizeR/clusize);
	ri->upc->start_cluster = cpu_to_le32(upccno);
	ri->upcoffs = c2o(&ri->sb,upccno);

	/* allocate root directory */
	off_t rootcno = upccno+(ri->upcsize/clusize);
	ri->sb.rootdir_cluster = cpu_to_le32(rootcno);

	exfat_debug("ROOTOFS %ld",ROOTOFS(ri));
	return 0;
}

#endif

