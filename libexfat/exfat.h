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
#define IS_CONTIGUOUS(node) (((node).flags & EXFAT_ATTRIB_CONTIGUOUS) != 0)
#define BLOCK_SIZE(sb) (1 << (sb).block_bits)
#define CLUSTER_SIZE(sb) (BLOCK_SIZE(sb) << (sb).bpc_bits)
#define CLUSTER_INVALID(c) ((c) == EXFAT_CLUSTER_BAD || (c) == EXFAT_CLUSTER_END)

struct exfat
{
	struct exfat_super_block* sb;
	int fd;
	time_t mount_time;
	le16_t* upcase;
	size_t upcase_chars;
};

struct exfat_node
{
	cluster_t start_cluster;
	int flags;
	uint64_t size;
	time_t mtime, atime;
	le16_t name[EXFAT_NAME_MAX + 1];
};

struct exfat_iterator
{
	cluster_t cluster;
	off_t offset;
	int contiguous;
	char* chunk;
};

extern int exfat_errors;

void exfat_bug(const char* format, ...);
void exfat_error(const char* format, ...);
void exfat_warn(const char* format, ...);
void exfat_debug(const char* format, ...);

void exfat_read_raw(void* buffer, size_t size, off_t offset, int fd);
ssize_t exfat_read(const struct exfat* ef, const struct exfat_node* node,
		void* buffer, size_t size, off_t offset);

void exfat_opendir(struct exfat_node* node, struct exfat_iterator* it);
void exfat_closedir(struct exfat_iterator* it);
int exfat_readdir(struct exfat* ef, struct exfat_node* node,
		struct exfat_iterator* it);
int exfat_lookup(struct exfat* ef, struct exfat_node* node,
		const char* path);

off_t exfat_c2o(const struct exfat* ef, cluster_t cluster);
cluster_t exfat_next_cluster(const struct exfat* ef, cluster_t cluster,
		int contiguous);
cluster_t exfat_advance_cluster(const struct exfat* ef, cluster_t cluster,
		int contiguous, uint32_t count);

int exfat_mount(struct exfat* ef, const char* spec);
void exfat_unmount(struct exfat* ef);
void exfat_stat(const struct exfat_node* node, struct stat *stbuf);
time_t exfat_exfat2unix(le16_t date, le16_t time);
void exfat_get_name(const struct exfat_node* node, char* buffer, size_t n);

int utf16_to_utf8(char* output, const le16_t* input, size_t outsize,
		size_t insize);
int utf8_to_utf16(le16_t* output, const char* input, size_t outsize,
		size_t insize);

#endif /* ifndef EXFAT_H_INCLUDED */
