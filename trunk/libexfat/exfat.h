/*
 *  exfat.h
 *  Definitions of structures and constants used in exFAT file system
 *  implementation.
 *
 *  Created by Andrew Nayenko on 29.08.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#ifndef EXFAT_H_INCLUDED
#define EXFAT_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "exfatfs.h"

#define EXFAT_VERSION_MAJOR 0
#define EXFAT_VERSION_MINOR 5

#define EXFAT_NAME_MAX 256
#define EXFAT_ATTRIB_CONTIGUOUS 0x10000
#define EXFAT_ATTRIB_CACHED     0x20000
#define EXFAT_ATTRIB_DIRTY      0x40000
#define EXFAT_ATTRIB_UNLINKED   0x80000
#define IS_CONTIGUOUS(node) (((node).flags & EXFAT_ATTRIB_CONTIGUOUS) != 0)
#define BLOCK_SIZE(sb) (1 << (sb).block_bits)
#define CLUSTER_SIZE(sb) (BLOCK_SIZE(sb) << (sb).bpc_bits)
#define CLUSTER_INVALID(c) ((c) == EXFAT_CLUSTER_BAD || (c) == EXFAT_CLUSTER_END)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define DIV_ROUND_UP(x, d) (((x) + (d) - 1) / (d))

struct exfat_node
{
	struct exfat_node* parent;
	struct exfat_node* child;
	struct exfat_node* next;
	struct exfat_node* prev;

	int references;
	uint32_t fptr_index;
	cluster_t fptr_cluster;
	cluster_t entry_cluster;
	off_t entry_offset;
	cluster_t start_cluster;
	int flags;
	uint64_t size;
	time_t mtime, atime;
	le16_t name[EXFAT_NAME_MAX + 1];
};

struct exfat
{
	struct exfat_super_block* sb;
	int fd;
	le16_t* upcase;
	size_t upcase_chars;
	struct exfat_node* root;
	struct
	{
		cluster_t start_cluster;
		uint32_t size;				/* in bits */
		uint8_t* chunk;
		uint32_t chunk_size;		/* in bits */
		int dirty;
	}
	cmap;
	void* zero_block;
	int dmask, fmask;
};

/* in-core nodes iterator */
struct exfat_iterator
{
	struct exfat_node* parent;
	struct exfat_node* current;
};

extern int exfat_errors;

void exfat_bug(const char* format, ...)
	__attribute__((format(printf, 1, 2), noreturn));
void exfat_error(const char* format, ...)
	__attribute__((format(printf, 1, 2)));
void exfat_warn(const char* format, ...)
	__attribute__((format(printf, 1, 2)));
void exfat_debug(const char* format, ...)
	__attribute__((format(printf, 1, 2)));

void exfat_read_raw(void* buffer, size_t size, off_t offset, int fd);
void exfat_write_raw(const void* buffer, size_t size, off_t offset, int fd);
ssize_t exfat_read(const struct exfat* ef, struct exfat_node* node,
		void* buffer, size_t size, off_t offset);
ssize_t exfat_write(struct exfat* ef, struct exfat_node* node,
		const void* buffer, size_t size, off_t offset);

int exfat_opendir(struct exfat* ef, struct exfat_node* dir,
		struct exfat_iterator* it);
void exfat_closedir(struct exfat* ef, struct exfat_iterator* it);
struct exfat_node* exfat_readdir(struct exfat* ef, struct exfat_iterator* it);
int exfat_lookup(struct exfat* ef, struct exfat_node** node,
		const char* path);
int exfat_split(struct exfat* ef, struct exfat_node** node, le16_t* name,
		const char* path);

off_t exfat_c2o(const struct exfat* ef, cluster_t cluster);
cluster_t exfat_next_cluster(const struct exfat* ef,
		const struct exfat_node* node, cluster_t cluster);
cluster_t exfat_advance_cluster(const struct exfat* ef,
		struct exfat_node* node, uint32_t count);
void exfat_flush_cmap(struct exfat* ef);
int exfat_truncate(struct exfat* ef, struct exfat_node* node, uint64_t size);

void exfat_stat(const struct exfat* ef, const struct exfat_node* node,
		struct stat* stbuf);
time_t exfat_exfat2unix(le16_t date, le16_t time);
void exfat_unix2exfat(time_t unix_time, le16_t* date, le16_t* time);
void exfat_get_name(const struct exfat_node* node, char* buffer, size_t n);
uint16_t exfat_start_checksum(const struct exfat_file* entry);
uint16_t exfat_add_checksum(const void* entry, uint16_t sum);
le16_t exfat_calc_checksum(const struct exfat_file* meta1,
		const struct exfat_file_info* meta2, const le16_t* name);
le16_t exfat_calc_name_hash(const struct exfat* ef, const le16_t* name);

int utf16_to_utf8(char* output, const le16_t* input, size_t outsize,
		size_t insize);
int utf8_to_utf16(le16_t* output, const char* input, size_t outsize,
		size_t insize);
size_t utf16_length(const le16_t* str);

struct exfat_node* exfat_get_node(struct exfat_node* node);
void exfat_put_node(struct exfat* ef, struct exfat_node* node);
int exfat_cache_directory(struct exfat* ef, struct exfat_node* dir);
void exfat_reset_cache(struct exfat* ef);
void exfat_flush_node(struct exfat* ef, struct exfat_node* node);
int exfat_unlink(struct exfat* ef, struct exfat_node* node);
int exfat_rmdir(struct exfat* ef, struct exfat_node* node);
int exfat_mknod(struct exfat* ef, const char* path);
int exfat_mkdir(struct exfat* ef, const char* path);

int exfat_mount(struct exfat* ef, const char* spec, const char* options);
void exfat_unmount(struct exfat* ef);

#endif /* ifndef EXFAT_H_INCLUDED */
