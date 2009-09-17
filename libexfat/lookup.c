/*
 *  lookup.c
 *  exFAT file system implementation library.
 *
 *  Created by Andrew Nayenko on 02.09.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#include "exfat.h"
#include <string.h>
#include <errno.h>
#include <inttypes.h>

void exfat_opendir(struct exfat_node* node, struct exfat_iterator* it)
{
	if (!(node->flags & EXFAT_ATTRIB_DIR))
		exfat_bug("`%s' is not a directory", node->name);
	it->cluster = node->start_cluster;
	it->offset = 0;
	it->contiguous = IS_CONTIGUOUS(*node);
	it->chunk = NULL;
}

void exfat_closedir(struct exfat_iterator* it)
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
int exfat_readdir(struct exfat* ef, struct exfat_node* node,
		struct exfat_iterator* it)
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
				return -EIO;
			}
			return -ENOENT; /* that's OK, means end of directory */

		case EXFAT_ENTRY_FILE:
			if (continuations != 0)
			{
				exfat_error("expected %hhu continuations before new entry",
						continuations);
				return -EIO;
			}
			memset(node, 0, sizeof(struct exfat_node));
			file = (const struct exfat_file*) entry;
			node->flags = le16_to_cpu(file->attrib);
			node->mtime = exfat_exfat2unix(file->mdate, file->mtime);
			node->atime = exfat_exfat2unix(file->adate, file->atime);
			namep = node->name;
			continuations = file->continuations;
			/* each file entry must have at least 2 continuations:
			   info and name */
			if (continuations < 2)
			{
				exfat_error("too few continuations (%hhu)", continuations);
				return -EIO;
			}
			break;

		case EXFAT_ENTRY_FILE_INFO:
			if (continuations < 2)
			{
				exfat_error("unexpected continuation (%hhu)",
						continuations);
				return -EIO;
			}
			file_info = (const struct exfat_file_info*) entry;
			node->size = le64_to_cpu(file_info->size);
			node->start_cluster = le32_to_cpu(file_info->start_cluster);
			if (file_info->flag == EXFAT_FLAG_CONTIGUOUS)
				node->flags |= EXFAT_ATTRIB_CONTIGUOUS;
			--continuations;
			break;

		case EXFAT_ENTRY_FILE_NAME:
			if (continuations == 0)
			{
				exfat_error("unexpected continuation");
				return -EIO;
			}
			file_name = (const struct exfat_file_name*) entry;
			memcpy(namep, file_name->name, EXFAT_ENAME_MAX * sizeof(le16_t));
			namep += EXFAT_ENAME_MAX;
			if (--continuations == 0)
				return 0; /* entry completed */
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
			if (le64_to_cpu(bitmap->size) !=
					((le32_to_cpu(ef->sb->cluster_count) + 7) / 8))
			{
				exfat_error("invalid bitmap size: %"PRIu64" (expected %u)",
						le64_to_cpu(bitmap->size),
						(le32_to_cpu(ef->sb->cluster_count) + 7) / 8);
				return -EIO;
			}
			break;

		case EXFAT_ENTRY_LABEL:
			label = (const struct exfat_label*) entry;
			if (label->length > EXFAT_ENAME_MAX)
			{
				exfat_error("too long label (%hhu chars)", label->length);
				return -EIO;
			}
			break;
		}

		/* fetch the next cluster if needed */
		if ((it->offset & (CLUSTER_SIZE(*ef->sb) - 1)) == 0)
		{
			it->cluster = exfat_next_cluster(ef, it->cluster, it->contiguous);
			if (CLUSTER_INVALID(it->cluster))
			{
				exfat_error("invalid cluster while reading directory");
				return -EIO;
			}
			exfat_read_raw(it->chunk, CLUSTER_SIZE(*ef->sb),
					exfat_c2o(ef, it->cluster), ef->fd);
		}
	}
	/* we never reach here */
}

static int compare_char(struct exfat* ef, uint16_t a, uint16_t b)
{
	if (a >= ef->upcase_chars || b >= ef->upcase_chars)
		return a - b;
	else
		return le16_to_cpu(ef->upcase[a]) - le16_to_cpu(ef->upcase[b]);
}

static int compare_name(struct exfat* ef, const le16_t* a, const le16_t* b)
{
	while (le16_to_cpu(*a) && le16_to_cpu(*b))
	{
		if (compare_char(ef, le16_to_cpu(*a), le16_to_cpu(*b)) != 0)
			break;
		a++;
		b++;
	}
	return le16_to_cpu(*a) - le16_to_cpu(*b);
}

static int lookup_name(struct exfat* ef, struct exfat_node* node,
		const le16_t* name)
{
	struct exfat_iterator it;

	exfat_opendir(node, &it);
	while (exfat_readdir(ef, node, &it) == 0)
	{
		if (compare_name(ef, name, node->name) == 0)
		{
			exfat_closedir(&it);
			return 0;
		}
	}
	exfat_closedir(&it);
	return -ENOENT;
}

int exfat_lookup(struct exfat* ef, struct exfat_node* node,
		const char* path)
{
	le16_t buffer[EXFAT_NAME_MAX + 1];
	int rc;
	le16_t* p;
	le16_t* subpath;

	if (strlen(path) > EXFAT_NAME_MAX)
	{
		exfat_error("file name `%s' is too long", path);
		return -ENAMETOOLONG;
	}

	rc = utf8_to_utf16(buffer, path, EXFAT_NAME_MAX, strlen(path));
	if (rc != 0)
		return rc;

	/* start from the root directory */
	node->flags = EXFAT_ATTRIB_DIR;
	node->size = 0;
	node->start_cluster = le32_to_cpu(ef->sb->rootdir_cluster);
	node->name[0] = cpu_to_le16('\0');
	/* exFAT does not have time attributes for the root directory */
	node->mtime = ef->mount_time;
	node->atime = ef->mount_time;

	for (subpath = p = buffer; p; subpath = p + 1)
	{
		while (le16_to_cpu(*subpath) == '/')
			subpath++;		/* skip leading slashes */
		for (p = subpath; ; p++)
		{
			if (le16_to_cpu(*p) == '\0')
			{
				p = NULL;
				break;
			}
			if (le16_to_cpu(*p) == '/')
			{
				*p = cpu_to_le16('\0');
				break;
			}
		}
		if (le16_to_cpu(*subpath) == '\0')
			break;			/* skip trailing slashes */
		if (le16_to_cpu(subpath[0]) == '.' && le16_to_cpu(subpath[1]) == '\0')
			continue;		/* skip "." component */
		if (lookup_name(ef, node, subpath) != 0)
			return -ENOENT;
	}
	return 0;
}
