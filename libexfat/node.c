/*
 *  node.c
 *  exFAT file system implementation library.
 *
 *  Created by Andrew Nayenko on 09.10.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#include "exfat.h"
#include <errno.h>
#include <string.h>
#include <inttypes.h>

/* on-disk nodes iterator */
struct iterator
{
	cluster_t cluster;
	off_t offset;
	int contiguous;
	char* chunk;
};

struct exfat_node* exfat_get_node(struct exfat_node* node)
{
	/* if we switch to multi-threaded mode we will need atomic
	   increment here and atomic decrement in exfat_put_node() */
	node->references++;
	return node;
}

void exfat_put_node(struct exfat* ef, struct exfat_node* node)
{
	if (--node->references < 0)
	{
		char buffer[EXFAT_NAME_MAX + 1];
		exfat_get_name(node, buffer, EXFAT_NAME_MAX);
		exfat_bug("reference counter of `%s' is below zero", buffer);
	}

	if (node->references == 0)
	{
		if (node->flags & EXFAT_ATTRIB_DIRTY)
			exfat_flush_node(ef, node);
		if (node->flags & EXFAT_ATTRIB_UNLINKED)
		{
			/* free all clusters and node structure itself */
			exfat_truncate(ef, node, 0);
			free(node);
		}
		if (ef->cmap.dirty)
			exfat_flush_cmap(ef);
	}
}

static int opendir(struct exfat* ef, const struct exfat_node* dir,
		struct iterator* it)
{
	if (!(dir->flags & EXFAT_ATTRIB_DIR))
		exfat_bug("`%s' is not a directory", dir->name);
	it->cluster = dir->start_cluster;
	it->offset = 0;
	it->contiguous = IS_CONTIGUOUS(*dir);
	it->chunk = malloc(CLUSTER_SIZE(*ef->sb));
	if (it->chunk == NULL)
	{
		exfat_error("out of memory");
		return -ENOMEM;
	}
	exfat_read_raw(it->chunk, CLUSTER_SIZE(*ef->sb),
			exfat_c2o(ef, it->cluster), ef->fd);
	return 0;
}

static void closedir(struct iterator* it)
{
	it->cluster = 0;
	it->offset = 0;
	it->contiguous = 0;
	free(it->chunk);
	it->chunk = NULL;
}

static int fetch_next_entry(struct exfat* ef, const struct exfat_node* parent,
		struct iterator* it)
{
	/* move iterator to the next entry in the directory */
	it->offset += sizeof(struct exfat_entry);
	/* fetch the next cluster if needed */
	if ((it->offset & (CLUSTER_SIZE(*ef->sb) - 1)) == 0)
	{
		it->cluster = exfat_next_cluster(ef, parent, it->cluster);
		if (CLUSTER_INVALID(it->cluster))
		{
			exfat_error("invalid cluster while reading directory");
			return 1;
		}
		exfat_read_raw(it->chunk, CLUSTER_SIZE(*ef->sb),
				exfat_c2o(ef, it->cluster), ef->fd);
	}
	return 0;
}

static struct exfat_node* allocate_node(void)
{
	struct exfat_node* node = malloc(sizeof(struct exfat_node));
	if (node == NULL)
	{
		exfat_error("failed to allocate node");
		return NULL;
	}
	memset(node, 0, sizeof(struct exfat_node));
	return node;
}

static void init_node_meta1(struct exfat_node* node,
		const struct exfat_file* meta1)
{
	node->flags = le16_to_cpu(meta1->attrib);
	node->mtime = exfat_exfat2unix(meta1->mdate, meta1->mtime);
	node->atime = exfat_exfat2unix(meta1->adate, meta1->atime);
}

static void init_node_meta2(struct exfat_node* node,
		const struct exfat_file_info* meta2)
{
	node->size = le64_to_cpu(meta2->size);
	node->start_cluster = le32_to_cpu(meta2->start_cluster);
	node->fptr_cluster = node->start_cluster;
	if (meta2->flag == EXFAT_FLAG_CONTIGUOUS)
		node->flags |= EXFAT_ATTRIB_CONTIGUOUS;
}

/*
 * Reads one entry in directory at position pointed by iterator and fills
 * node structure.
 */
static int readdir(struct exfat* ef, const struct exfat_node* parent,
		struct exfat_node** node, struct iterator* it)
{
	const struct exfat_entry* entry;
	const struct exfat_file* file;
	const struct exfat_file_info* file_info;
	const struct exfat_file_name* file_name;
	const struct exfat_upcase* upcase;
	const struct exfat_bitmap* bitmap;
	const struct exfat_label* label;
	uint8_t continuations = 0;
	le16_t* namep = NULL;
	uint16_t reference_checksum = 0;
	uint16_t actual_checksum = 0;

	*node = NULL;

	for (;;)
	{
		/* every directory (even empty one) occupies at least one cluster and
		   must contain EOD entry */
		entry = (const struct exfat_entry*)
				(it->chunk + it->offset % CLUSTER_SIZE(*ef->sb));

		switch (entry->type)
		{
		case EXFAT_ENTRY_EOD:
			if (continuations != 0)
			{
				exfat_error("expected %hhu continuations before EOD",
						continuations);
				goto error;
			}
			return -ENOENT; /* that's OK, means end of directory */

		case EXFAT_ENTRY_FILE:
			if (continuations != 0)
			{
				exfat_error("expected %hhu continuations before new entry",
						continuations);
				goto error;
			}
			file = (const struct exfat_file*) entry;
			continuations = file->continuations;
			/* each file entry must have at least 2 continuations:
			   info and name */
			if (continuations < 2)
			{
				exfat_error("too few continuations (%hhu)", continuations);
				return -EIO;
			}
			reference_checksum = le16_to_cpu(file->checksum);
			actual_checksum = exfat_start_checksum(file);
			*node = allocate_node();
			if (*node == NULL)
				return -ENOMEM;
			/* new node has zero reference counter */
			(*node)->entry_cluster = it->cluster;
			(*node)->entry_offset = it->offset % CLUSTER_SIZE(*ef->sb);
			init_node_meta1(*node, file);
			namep = (*node)->name;
			break;

		case EXFAT_ENTRY_FILE_INFO:
			if (continuations < 2)
			{
				exfat_error("unexpected continuation (%hhu)",
						continuations);
				goto error;
			}
			file_info = (const struct exfat_file_info*) entry;
			init_node_meta2(*node, file_info);
			actual_checksum = exfat_add_checksum(entry, actual_checksum);
			/* There are two fields that contain file size. Maybe they plan
			   to add compression support in the future and one of those
			   fields is visible (uncompressed) size and the other is real
			   (compressed) size. Anyway, currently it looks like exFAT does
			   not support compression and both fields must be equal. */
			if (le64_to_cpu(file_info->real_size) != (*node)->size)
			{
				exfat_error("real size does not equal to size "
						"(%"PRIu64" != %"PRIu64")",
						le64_to_cpu(file_info->real_size), (*node)->size);
				goto error;
			}
			/* directories must be aligned on at cluster boundary */
			if (((*node)->flags & EXFAT_ATTRIB_DIR) &&
				(*node)->size % CLUSTER_SIZE(*ef->sb) != 0)
			{
				char buffer[EXFAT_NAME_MAX + 1];

				exfat_get_name(*node, buffer, EXFAT_NAME_MAX);
				exfat_error("directory `%s' has invalid size %"PRIu64" bytes",
						buffer, (*node)->size);
				goto error;
			}
			--continuations;
			break;

		case EXFAT_ENTRY_FILE_NAME:
			if (continuations == 0)
			{
				exfat_error("unexpected continuation");
				goto error;
			}
			file_name = (const struct exfat_file_name*) entry;
			actual_checksum = exfat_add_checksum(entry, actual_checksum);

			memcpy(namep, file_name->name, EXFAT_ENAME_MAX * sizeof(le16_t));
			namep += EXFAT_ENAME_MAX;
			if (--continuations == 0)
			{
				if (actual_checksum != reference_checksum)
				{
					exfat_error("invalid checksum (0x%hx != 0x%hx)",
							actual_checksum, reference_checksum);
					return -EIO;
				}
				if (fetch_next_entry(ef, parent, it) != 0)
					goto error;
				return 0; /* entry completed */
			}
			break;

		case EXFAT_ENTRY_UPCASE:
			if (ef->upcase != NULL)
				break;
			upcase = (const struct exfat_upcase*) entry;
			if (CLUSTER_INVALID(le32_to_cpu(upcase->start_cluster)))
			{
				exfat_error("invalid cluster in upcase table");
				return -EIO;
			}
			if (le64_to_cpu(upcase->size) == 0 ||
				le64_to_cpu(upcase->size) > 0xffff * sizeof(uint16_t) ||
				le64_to_cpu(upcase->size) % sizeof(uint16_t) != 0)
			{
				exfat_error("bad upcase table size (%"PRIu64" bytes)",
						le64_to_cpu(upcase->size));
				return -EIO;
			}
			ef->upcase = malloc(le64_to_cpu(upcase->size));
			if (ef->upcase == NULL)
			{
				exfat_error("failed to allocate upcase table (%"PRIu64" bytes)",
						le64_to_cpu(upcase->size));
				return -ENOMEM;
			}
			ef->upcase_chars = le64_to_cpu(upcase->size) / sizeof(le16_t);

			exfat_read_raw(ef->upcase, le64_to_cpu(upcase->size),
					exfat_c2o(ef, le32_to_cpu(upcase->start_cluster)), ef->fd);
			break;

		case EXFAT_ENTRY_BITMAP:
			bitmap = (const struct exfat_bitmap*) entry;
			if (CLUSTER_INVALID(le32_to_cpu(bitmap->start_cluster)))
			{
				exfat_error("invalid cluster in clusters bitmap");
 				return -EIO;
			}
			ef->cmap.size = le32_to_cpu(ef->sb->cluster_count) -
				EXFAT_FIRST_DATA_CLUSTER;
			if (le64_to_cpu(bitmap->size) != (ef->cmap.size + 7) / 8)
			{
				exfat_error("invalid bitmap size: %"PRIu64" (expected %u)",
						le64_to_cpu(bitmap->size), (ef->cmap.size + 7) / 8);
				return -EIO;
			}
			ef->cmap.start_cluster = le32_to_cpu(bitmap->start_cluster);
			/* FIXME bitmap can be rather big, up to 512 MB */
			ef->cmap.chunk_size = ef->cmap.size;
			ef->cmap.chunk = malloc(le64_to_cpu(bitmap->size));
			if (ef->cmap.chunk == NULL)
			{
				exfat_error("failed to allocate clusters map chunk "
						"(%"PRIu64" bytes)", le64_to_cpu(bitmap->size));
				return -ENOMEM;
			}

			exfat_read_raw(ef->cmap.chunk, le64_to_cpu(bitmap->size),
					exfat_c2o(ef, ef->cmap.start_cluster), ef->fd);
			break;

		case EXFAT_ENTRY_LABEL:
			label = (const struct exfat_label*) entry;
			if (label->length > EXFAT_ENAME_MAX)
			{
				exfat_error("too long label (%hhu chars)", label->length);
				return -EIO;
			}
			break;

		default:
			if (entry->type & EXFAT_ENTRY_VALID)
			{
				exfat_error("unknown entry type 0x%hhu", entry->type);
				goto error;
			}
			break;
		}

		if (fetch_next_entry(ef, parent, it) != 0)
			goto error;
	}
	/* we never reach here */

error:
	free(*node);
	*node = NULL;
	return -EIO;
}

int exfat_cache_directory(struct exfat* ef, struct exfat_node* dir)
{
	struct iterator it;
	int rc;
	struct exfat_node* node;
	struct exfat_node* current = NULL;

	if (dir->flags & EXFAT_ATTRIB_CACHED)
		return 0; /* already cached */

	rc = opendir(ef, dir, &it);
	if (rc != 0)
		return rc;
	while ((rc = readdir(ef, dir, &node, &it)) == 0)
	{
		node->parent = dir;
		if (current != NULL)
		{
			current->next = node;
			node->prev = current;
		}
		else
			dir->child = node;

		current = node;
	}
	closedir(&it);

	if (rc != -ENOENT)
	{
		/* rollback */
		for (current = dir->child; current; current = node)
		{
			node = current->next;
			free(current);
		}
		dir->child = NULL;
		return rc;
	}

	dir->flags |= EXFAT_ATTRIB_CACHED;
	return 0;
}

static void reset_cache(struct exfat* ef, struct exfat_node* node)
{
	struct exfat_node* child;
	struct exfat_node* next;

	for (child = node->child; child; child = next)
	{
		reset_cache(ef, child);
		next = child->next;
		free(child);
	}
	if (node->references != 0)
	{
		char buffer[EXFAT_NAME_MAX + 1];
		exfat_get_name(node, buffer, EXFAT_NAME_MAX);
		exfat_warn("non-zero reference counter (%d) for `%s'",
				node->references, buffer);
	}
	while (node->references--)
		exfat_put_node(ef, node);
	node->child = NULL;
	node->flags &= ~EXFAT_ATTRIB_CACHED;
}

void exfat_reset_cache(struct exfat* ef)
{
	reset_cache(ef, ef->root);
}

void next_entry(struct exfat* ef, const struct exfat_node* parent,
		cluster_t* cluster, off_t* offset)
{
	if (*offset + sizeof(struct exfat_entry) == CLUSTER_SIZE(*ef->sb))
	{
		/* next cluster cannot be invalid */
		*cluster = exfat_next_cluster(ef, parent, *cluster);
		*offset = 0;
	}
	else
		*offset += sizeof(struct exfat_entry);

}

void exfat_flush_node(struct exfat* ef, struct exfat_node* node)
{
	cluster_t cluster;
	off_t offset;
	off_t meta1_offset, meta2_offset;
	struct exfat_file meta1;
	struct exfat_file_info meta2;

	if (node->parent == NULL)
		return; /* do not flush unlinked node */

	cluster = node->entry_cluster;
	offset = node->entry_offset;
	meta1_offset = exfat_c2o(ef, cluster) + offset;
	next_entry(ef, node->parent, &cluster, &offset);
	meta2_offset = exfat_c2o(ef, cluster) + offset;

	exfat_read_raw(&meta1, sizeof(meta1), meta1_offset, ef->fd);
	if (meta1.type != EXFAT_ENTRY_FILE)
		exfat_bug("invalid type of meta1: 0x%hhx", meta1.type);
	meta1.attrib = cpu_to_le16(node->flags);
	exfat_unix2exfat(node->mtime, &meta1.mdate, &meta1.mtime);
	exfat_unix2exfat(node->atime, &meta1.adate, &meta1.atime);

	exfat_read_raw(&meta2, sizeof(meta2), meta2_offset, ef->fd);
	if (meta2.type != EXFAT_ENTRY_FILE_INFO)
		exfat_bug("invalid type of meta2: 0x%hhx", meta2.type);
	meta2.size = meta2.real_size = cpu_to_le64(node->size);
	meta2.start_cluster = cpu_to_le32(node->start_cluster);
	/* empty files must be marked as fragmented */
	if (node->size != 0 && IS_CONTIGUOUS(*node))
		meta2.flag = EXFAT_FLAG_CONTIGUOUS;
	else
		meta2.flag = EXFAT_FLAG_FRAGMENTED;
	/* name hash remains unchanged, no need to recalculate it */

	meta1.checksum = exfat_calc_checksum(&meta1, &meta2, node->name);

	exfat_write_raw(&meta1, sizeof(meta1), meta1_offset, ef->fd);
	exfat_write_raw(&meta2, sizeof(meta2), meta2_offset, ef->fd);

	node->flags &= ~EXFAT_ATTRIB_DIRTY;
}

static void erase_entry(struct exfat* ef, struct exfat_node* node)
{
	cluster_t cluster = node->entry_cluster;
	off_t offset = node->entry_offset;
	int name_entries = DIV_ROUND_UP(utf16_length(node->name), EXFAT_ENAME_MAX);
	uint8_t entry_type;

	entry_type = EXFAT_ENTRY_FILE & ~EXFAT_ENTRY_VALID;
	exfat_write_raw(&entry_type, 1, exfat_c2o(ef, cluster) + offset, ef->fd);

	next_entry(ef, node->parent, &cluster, &offset);
	entry_type = EXFAT_ENTRY_FILE_INFO & ~EXFAT_ENTRY_VALID;
	exfat_write_raw(&entry_type, 1, exfat_c2o(ef, cluster) + offset, ef->fd);

	while (name_entries--)
	{
		next_entry(ef, node->parent, &cluster, &offset);
		entry_type = EXFAT_ENTRY_FILE_NAME & ~EXFAT_ENTRY_VALID;
		exfat_write_raw(&entry_type, 1, exfat_c2o(ef, cluster) + offset,
				ef->fd);
	}
}

static void delete(struct exfat* ef, struct exfat_node* node)
{
	erase_entry(ef, node);
	if (node->prev)
		node->prev->next = node->next;
	else /* this is the first node in the list */
		node->parent->child = node->next;
	if (node->next)
		node->next->prev = node->prev;
	node->parent = NULL;
	node->prev = NULL;
	node->next = NULL;
	/* file clusters will be freed when node reference counter becomes 0 */
	node->flags |= EXFAT_ATTRIB_UNLINKED;
}

int exfat_unlink(struct exfat* ef, struct exfat_node* node)
{
	if (node->flags & EXFAT_ATTRIB_DIR)
		return -EISDIR;
	delete(ef, node);
	return 0;
}

int exfat_rmdir(struct exfat* ef, struct exfat_node* node)
{
	if (!(node->flags & EXFAT_ATTRIB_DIR))
		return -ENOTDIR;
	/* check that directory is empty */
	exfat_cache_directory(ef, node);
	if (node->child)
		return -ENOTEMPTY;
	delete(ef, node);
	return 0;
}

static int grow_directory(struct exfat* ef, struct exfat_node* dir,
		uint64_t asize, uint32_t difference)
{
	return exfat_truncate(ef, dir,
			DIV_ROUND_UP(asize + difference, CLUSTER_SIZE(*ef->sb))
				* CLUSTER_SIZE(*ef->sb));
}

static void write_eod(struct exfat* ef, struct exfat_node* dir,
		cluster_t cluster, off_t offset, int seek)
{
	struct exfat_entry eod;

	while (seek--)
		next_entry(ef, dir, &cluster, &offset);
	memset(&eod, 0, sizeof(struct exfat_entry));
	exfat_write_raw(&eod, sizeof(struct exfat_entry),
			exfat_c2o(ef, cluster) + offset, ef->fd);
}

static int find_slot(struct exfat* ef, struct exfat_node* dir,
		cluster_t* cluster, off_t* offset, int subentries)
{
	struct iterator it;
	int rc;
	const struct exfat_entry* entry;
	int contiguous = 0;

	rc = opendir(ef, dir, &it);
	if (rc != 0)
		return rc;
	for (;;)
	{
		if (contiguous == 0)
		{
			*cluster = it.cluster;
			*offset = it.offset % CLUSTER_SIZE(*ef->sb);
		}
		entry = (const struct exfat_entry*)
				(it.chunk + it.offset % CLUSTER_SIZE(*ef->sb));
		if (entry->type == EXFAT_ENTRY_EOD)
		{
			rc = grow_directory(ef, dir,
					it.offset + sizeof(struct exfat_entry), /* actual size */
					(subentries - contiguous) * sizeof(struct exfat_entry));
			if (rc != 0)
			{
				closedir(&it);
				return rc;
			}
			write_eod(ef, dir, *cluster, *offset, subentries - contiguous);
			break;
		}
		if (entry->type & EXFAT_ENTRY_VALID)
			contiguous = 0;
		else
			contiguous++;
		if (contiguous == subentries)
			break;	/* suitable slot it found */
		if (fetch_next_entry(ef, dir, &it) != 0)
		{
			closedir(&it);
			return -EIO;
		}
	}
	closedir(&it);
	return 0;
}

static int write_entry(struct exfat* ef, struct exfat_node* dir,
		const le16_t* name, cluster_t cluster, off_t offset, uint16_t attrib)
{
	struct exfat_node* node;
	struct exfat_file meta1;
	struct exfat_file_info meta2;
	const size_t name_length = utf16_length(name);
	const int name_entries = DIV_ROUND_UP(name_length, EXFAT_ENAME_MAX);
	int i;

	node = allocate_node();
	if (node == NULL)
		return -ENOMEM;
	node->entry_cluster = cluster;
	node->entry_offset = offset;
	memcpy(node->name, name, name_length * sizeof(le16_t));

	memset(&meta1, 0, sizeof(meta1));
	meta1.type = EXFAT_ENTRY_FILE;
	meta1.continuations = 1 + name_entries;
	meta1.attrib = cpu_to_le16(attrib);
	exfat_unix2exfat(time(NULL), &meta1.crdate, &meta1.crtime);
	meta1.adate = meta1.mdate = meta1.crdate;
	meta1.atime = meta1.mtime = meta1.crtime;
	/* crtime_cs and mtime_cs contain addition to the time in centiseconds;
	   just ignore those fields because we operate with 2 sec resolution */

	memset(&meta2, 0, sizeof(meta2));
	meta2.type = EXFAT_ENTRY_FILE_INFO;
	meta2.flag = EXFAT_FLAG_FRAGMENTED;
	meta2.name_length = name_length;
	meta2.name_hash = exfat_calc_name_hash(ef, node->name);
	meta2.start_cluster = cpu_to_le32(EXFAT_CLUSTER_FREE);

	meta1.checksum = exfat_calc_checksum(&meta1, &meta2, node->name);

	exfat_write_raw(&meta1, sizeof(meta1), exfat_c2o(ef, cluster) + offset,
			ef->fd);
	next_entry(ef, dir, &cluster, &offset);
	exfat_write_raw(&meta2, sizeof(meta2), exfat_c2o(ef, cluster) + offset,
			ef->fd);
	for (i = 0; i < name_entries; i++)
	{
		struct exfat_file_name name_entry = {EXFAT_ENTRY_FILE_NAME, 0};
		memcpy(name_entry.name, node->name + i * EXFAT_ENAME_MAX,
				EXFAT_ENAME_MAX * sizeof(le16_t));
		next_entry(ef, dir, &cluster, &offset);
		exfat_write_raw(&name_entry, sizeof(name_entry),
				exfat_c2o(ef, cluster) + offset, ef->fd);
	}

	init_node_meta1(node, &meta1);
	init_node_meta2(node, &meta2);

	node->parent = dir;
	if (dir->child)
	{
		dir->child->prev = node;
		node->next = dir->child;
	}
	dir->child = node;

	return 0;
}

static int create(struct exfat* ef, const char* path, uint16_t attrib)
{
	struct exfat_node* dir;
	cluster_t cluster = EXFAT_CLUSTER_BAD;
	off_t offset = -1;
	le16_t name[EXFAT_NAME_MAX + 1];
	int rc;

	/* FIXME filter name characters */

	rc = exfat_split(ef, &dir, name, path);
	if (rc != 0)
		return rc;

	rc = find_slot(ef, dir, &cluster, &offset,
			2 + DIV_ROUND_UP(utf16_length(name), EXFAT_ENAME_MAX));
	if (rc != 0)
	{
		exfat_put_node(ef, dir);
		return rc;
	}
	rc = write_entry(ef, dir, name, cluster, offset, attrib);
	exfat_put_node(ef, dir);
	return rc;
}

int exfat_mknod(struct exfat* ef, const char* path)
{
	return create(ef, path, EXFAT_ATTRIB_ARCH);
}

int exfat_mkdir(struct exfat* ef, const char* path)
{
	int rc;
	struct exfat_node* node;

	rc = create(ef, path, EXFAT_ATTRIB_ARCH | EXFAT_ATTRIB_DIR);
	if (rc != 0)
		return rc;
	rc = exfat_lookup(ef, &node, path);
	if (rc != 0)
		return 0;
	/* directories always have at least one cluster */
	rc = exfat_truncate(ef, node, CLUSTER_SIZE(*ef->sb));
	if (rc != 0)
	{
		delete(ef, node);
		exfat_put_node(ef, node);
		return rc;
	}
	exfat_put_node(ef, node);
	return 0;
}
