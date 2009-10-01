/*
 *  utils.c
 *  exFAT file system implementation library.
 *
 *  Created by Andrew Nayenko on 04.09.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#include "exfat.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

static uint64_t rootdir_size(const struct exfat* ef)
{
	uint64_t clusters = 0;
	cluster_t rootdir_cluster = le32_to_cpu(ef->sb->rootdir_cluster);

	while (!CLUSTER_INVALID(rootdir_cluster))
	{
		clusters++;
		/* root directory cannot be contiguous because there is no flag
		   to indicate this */
		rootdir_cluster = exfat_next_cluster(ef, rootdir_cluster, 0);
	}
	return clusters * CLUSTER_SIZE(*ef->sb);
}

int exfat_mount(struct exfat* ef, const char* spec)
{
	ef->sb = malloc(sizeof(struct exfat_super_block));
	if (ef->sb == NULL)
	{
		exfat_error("memory allocation failed");
		return -ENOMEM;
	}

	ef->fd = open(spec, O_RDONLY); /* currently read only */
	if (ef->fd < 0)
	{
		free(ef->sb);
		exfat_error("failed to open `%s'", spec);
		return -EIO;
	}

	exfat_read_raw(ef->sb, sizeof(struct exfat_super_block), 0, ef->fd);
	if (memcmp(ef->sb->oem_name, "EXFAT   ", 8) != 0)
	{
		close(ef->fd);
		free(ef->sb);
		exfat_error("exFAT file system is not found");
		return -EIO;
	}

	ef->upcase = NULL;
	ef->upcase_chars = 0;
	ef->rootdir_size = rootdir_size(ef);

	return 0;
}

void exfat_unmount(struct exfat* ef)
{
	close(ef->fd);
	ef->fd = 0;
	free(ef->sb);
	ef->sb = NULL;
	free(ef->upcase);
	ef->upcase = NULL;
	ef->upcase_chars = 0;
}

void exfat_stat(const struct exfat_node* node, struct stat *stbuf)
{
	memset(stbuf, 0, sizeof(struct stat));
	if (node->flags & EXFAT_ATTRIB_DIR)
		stbuf->st_mode = S_IFDIR | 0755;
	else
		stbuf->st_mode = S_IFREG | 0444;
	stbuf->st_nlink = 1;
	stbuf->st_size = node->size;
	stbuf->st_mtime = node->mtime;
	stbuf->st_atime = node->atime;
	stbuf->st_ctime = 0; /* unapplicable */
}

#define SEC_IN_MIN 60ll
#define SEC_IN_HOUR (60 * SEC_IN_MIN)
#define SEC_IN_DAY (24 * SEC_IN_HOUR)
#define SEC_IN_YEAR (365 * SEC_IN_DAY) /* not leap year */
/* Unix epoch started at 0:00:00 UTC 1 January 1970 */
#define UNIX_EPOCH_YEAR 1970
/* exFAT epoch started at 0:00:00 UTC 1 January 1980 */
#define EXFAT_EPOCH_YEAR 1980
/* number of years from Unix epoch to exFAT epoch */
#define EPOCH_DIFF_YEAR (EXFAT_EPOCH_YEAR - UNIX_EPOCH_YEAR)
/* number of seconds from Unix epoch to exFAT epoch (considering leap years) */
#define EPOCH_DIFF_SEC ((EPOCH_DIFF_YEAR*365 + EPOCH_DIFF_YEAR/4) * SEC_IN_DAY)

static const time_t days_in_year[] =
{
	/* Jan  Feb  Mar  Apr  May  Jun  Jul  Aug  Sep  Oct  Nov  Dec */
	0,   0,  31,  59,  90, 120, 151, 181, 212, 243, 273, 304, 334
};

union exfat_date
{
	uint16_t raw;
	struct
	{
		uint16_t day   : 5; /* 1-31 */
		uint16_t month : 4; /* 1-12 */
		uint16_t year  : 7; /* 1-127 (+1980) */
	};
};

union exfat_time
{
	uint16_t raw;
	struct
	{
		uint16_t twosec : 5; /* 0-29 (2 sec granularity) */
		uint16_t min    : 6; /* 0-59 */
		uint16_t hour   : 5; /* 0-23 */
	};
};

static time_t get_time_shift(void)
{
	struct timeval tv;
	struct timezone tz;

	if (gettimeofday(&tv, &tz) != 0)
		return 0;
	return tz.tz_minuteswest * SEC_IN_MIN;
}

time_t exfat_exfat2unix(le16_t date, le16_t time)
{
	union exfat_date edate;
	union exfat_time etime;
	time_t unix_time = EPOCH_DIFF_SEC;

	edate.raw = le16_to_cpu(date);
	etime.raw = le16_to_cpu(time);

	/*
	exfat_debug("%hu-%02hu-%02hu %hu:%02hu:%02hu",
			edate.year + 1980, edate.month, edate.day,
			etime.hour, etime.min, etime.twosec * 2);
	*/

	if (edate.day == 0 || edate.month == 0 || edate.month > 12)
	{
		exfat_error("bad date %hu-%02hu-%02hu",
				edate.year + EXFAT_EPOCH_YEAR, edate.month, edate.day);
		return 0;
	}
	if (etime.hour > 23 || etime.min > 59 || etime.twosec > 29)
	{
		exfat_error("bad time %hu:%02hu:%02hu",
			etime.hour, etime.min, etime.twosec * 2);
		return 0;
	}

	/* every 4th year between 1904 and 2096 is leap */
	unix_time += edate.year * SEC_IN_YEAR + edate.year / 4 * SEC_IN_DAY;
	unix_time += days_in_year[edate.month] * SEC_IN_DAY;
	/* if it's leap year and February has passed we should add 1 day */
	if (edate.year % 4 == 0 && edate.month > 2)
		unix_time += SEC_IN_DAY;
	unix_time += (edate.day - 1) * SEC_IN_DAY;

	unix_time += etime.hour * SEC_IN_HOUR;
	unix_time += etime.min * SEC_IN_MIN;
	/* exFAT represents time with 2 sec granularity */
	unix_time += etime.twosec * 2;

	/* exFAT stores timestamps in local time, so we correct it to UTC */
	unix_time += get_time_shift();

	return unix_time;
}

void exfat_get_name(const struct exfat_node* node, char* buffer, size_t n)
{
	if (utf16_to_utf8(buffer, node->name, n, EXFAT_NAME_MAX) != 0)
		exfat_bug("failed to convert name to UTF-8");
}
