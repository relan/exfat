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

void exfat_put_node(struct exfat_node* node)
{
	if (--node->references < 0)
	{
		char buffer[EXFAT_NAME_MAX + 1];
		exfat_get_name(node, buffer, EXFAT_NAME_MAX);
		exfat_bug("reference counter of `%s' is below zero", buffer);
	}
}

static void opendir(const struct exfat_node* dir, struct iterator* it)
{
	if (!(dir->flags & EXFAT_ATTRIB_DIR))
		exfat_bug("`%s' is not a directory", dir->name);
	it->cluster = dir->start_cluster;
	it->offset = 0;
	it->contiguous = IS_CONTIGUOUS(*dir);
	it->chunk = NULL;
}

static void closedir(struct iterator* it)
{
	it->cluster = 0;
	it->offset = 0;
	it->contiguous = 0;
	free(it->chunk);
	it->chunk = NULL;
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

	if (it->chunk == NULL)
	{
		it->chunk = malloc(CLUSTER_SIZE(*ef->sb));
		if (it->chunk == NULL)
		{
			exfat_error("out of memory");
			return -ENOMEM;
		}
		exfat_read_raw(it->chunk, CLUSTER_SIZE(*ef->sb),
				exfat_c2o(ef, it->cluster), ef->fd);
	}

	for (;;)
	{
		/* every directory (even empty one) occupies at least one cluster and
		   must contain EOD entry */
		entry = (const struct exfat_entry*)
				(it->chunk + it->offset % CLUSTER_SIZE(*ef->sb));
		/* move iterator to the next entry in the directory */
		it->offset += sizeof(struct exfat_entry);

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
			*node = malloc(sizeof(struct exfat_node));
			if (*node == NULL)
			{
				exfat_error("failed to allocate node");
				return -ENOMEM;
			}
			memset(*node, 0, sizeof(struct exfat_node));
			/* new node has zero reference counter */
			(*node)->meta1_offset = exfat_c2o(ef, it->cluster) +
					(it->offset - sizeof(struct exfat_entry)) %
					CLUSTER_SIZE(*ef->sb);
			(*node)->flags = le16_to_cpu(file->attrib);
			(*node)->mtime = exfat_exfat2unix(file->mdate, file->mtime);
			(*node)->atime = exfat_exfat2unix(file->adate, file->atime);
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
			actual_checksum = exfat_add_checksum(entry, actual_checksum);
			(*node)->meta2_offset = exfat_c2o(ef, it->cluster) +
					(it->offset - sizeof(struct exfat_entry)) %
					CLUSTER_SIZE(*ef->sb);
			(*node)->size = le64_to_cpu(file_info->size);
			(*node)->start_cluster = le32_to_cpu(file_info->start_cluster);
			(*node)->fptr_cluster = (*node)->start_cluster;
			if (file_info->flag == EXFAT_FLAG_CONTIGUOUS)
				(*node)->flags |= EXFAT_ATTRIB_CONTIGUOUS;
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

		/* fetch the next cluster if needed */
		if ((it->offset & (CLUSTER_SIZE(*ef->sb) - 1)) == 0)
		{
			it->cluster = exfat_next_cluster(ef, parent, it->cluster);
			if (CLUSTER_INVALID(it->cluster))
			{
				exfat_error("invalid cluster while reading directory");
				goto error;
			}
			exfat_read_raw(it->chunk, CLUSTER_SIZE(*ef->sb),
					exfat_c2o(ef, it->cluster), ef->fd);
		}
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

	opendir(dir, &it);
	while ((rc = readdir(ef, dir, &node, &it)) == 0)
	{
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

static void reset_cache(struct exfat_node* node)
{
	struct exfat_node* child;
	struct exfat_node* next;

	for (child = node->child; child; child = next)
	{
		reset_cache(child);
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
	node->child = NULL;
	node->flags &= ~EXFAT_ATTRIB_CACHED;
}

void exfat_reset_cache(struct exfat* ef)
{
	reset_cache(ef->root);
}

void exfat_flush_node(struct exfat* ef, const struct exfat_node* node)
{
	struct exfat_file meta1;
	struct exfat_file_info meta2;
	uint16_t checksum;
	uint8_t i;

	exfat_read_raw(&meta1, sizeof(meta1), node->meta1_offset, ef->fd);
	if (meta1.type != EXFAT_ENTRY_FILE)
		exfat_bug("invalid type of meta1: 0x%hhx", meta1.type);
	meta1.attrib = cpu_to_le16(node->flags);
	exfat_unix2exfat(node->mtime, &meta1.mdate, &meta1.mtime);
	exfat_unix2exfat(node->atime, &meta1.adate, &meta1.atime);

	exfat_read_raw(&meta2, sizeof(meta2), node->meta2_offset, ef->fd);
	if (meta2.type != EXFAT_ENTRY_FILE_INFO)
		exfat_bug("invalid type of meta2: 0x%hhx", meta2.type);
	meta2.size = cpu_to_le64(node->size);
	meta2.start_cluster = cpu_to_le32(node->start_cluster);
	meta2.flag = (IS_CONTIGUOUS(*node) ?
			EXFAT_FLAG_CONTIGUOUS : EXFAT_FLAG_FRAGMENTED);
	/* FIXME name hash */

	checksum = exfat_start_checksum(&meta1);
	checksum = exfat_add_checksum(&meta2, checksum);
	for (i = 1; i < meta1.continuations; i++)
	{
		struct exfat_file_name name = {EXFAT_ENTRY_FILE_NAME, 0};
		memcpy(name.name, node->name + (i - 1) * EXFAT_ENAME_MAX,
				EXFAT_ENAME_MAX * sizeof(le16_t));
		checksum = exfat_add_checksum(&name, checksum);
	}
	meta1.checksum = cpu_to_le16(checksum);

	exfat_write_raw(&meta1, sizeof(meta1), node->meta1_offset, ef->fd);
	exfat_write_raw(&meta2, sizeof(meta2), node->meta2_offset, ef->fd);
}
