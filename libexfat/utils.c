/*
 *  utils.c
 *  exFAT file system implementation library.
 *
 *  Created by Andrew Nayenko on 04.09.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#include "exfat.h"
#include <string.h>
#define _XOPEN_SOURCE /* for timezone in Linux */
#include <time.h>

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
/* number of days from Unix epoch to exFAT epoch (considering leap days) */
#define EPOCH_DIFF_DAYS (EPOCH_DIFF_YEAR * 365 + EPOCH_DIFF_YEAR / 4)
/* number of seconds from Unix epoch to exFAT epoch (considering leap days) */
#define EPOCH_DIFF_SEC (EPOCH_DIFF_DAYS * SEC_IN_DAY)
/* number of leap years passed from exFAT epoch to the specified year
   (excluding the specified year itself) */
#define LEAP_YEARS(year) ((EXFAT_EPOCH_YEAR + (year) - 1) / 4 \
		- (EXFAT_EPOCH_YEAR - 1) / 4)
/* checks whether the specified year is leap */
#define IS_LEAP_YEAR(year) ((EXFAT_EPOCH_YEAR + (year)) % 4 == 0)

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

time_t exfat_exfat2unix(le16_t date, le16_t time)
{
	union exfat_date edate;
	union exfat_time etime;
	time_t unix_time = EPOCH_DIFF_SEC;

	edate.raw = le16_to_cpu(date);
	etime.raw = le16_to_cpu(time);

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
	unix_time += edate.year * SEC_IN_YEAR + LEAP_YEARS(edate.year) * SEC_IN_DAY;
	unix_time += days_in_year[edate.month] * SEC_IN_DAY;
	/* if it's leap year and February has passed we should add 1 day */
	if ((EXFAT_EPOCH_YEAR + edate.year) % 4 == 0 && edate.month > 2)
		unix_time += SEC_IN_DAY;
	unix_time += (edate.day - 1) * SEC_IN_DAY;

	unix_time += etime.hour * SEC_IN_HOUR;
	unix_time += etime.min * SEC_IN_MIN;
	/* exFAT represents time with 2 sec granularity */
	unix_time += etime.twosec * 2;

	/* exFAT stores timestamps in local time, so we correct it to UTC */
	unix_time += timezone;

	return unix_time;
}

void exfat_unix2exfat(time_t unix_time, le16_t* date, le16_t* time)
{
	union exfat_date edate;
	union exfat_time etime;
	time_t shift = EPOCH_DIFF_SEC + timezone;
	int days;
	int i;

	/* time before exFAT epoch cannot be represented */
	if (unix_time < shift)
		unix_time = shift;

	unix_time -= shift;

	days = unix_time / SEC_IN_DAY;
	edate.year = (4 * days) / (4 * 365 + 1);
	days -= edate.year * 365 + LEAP_YEARS(edate.year);
	for (i = 1; i <= 12; i++)
	{
		int leap_day = (IS_LEAP_YEAR(edate.year) && i == 2);
		int leap_sub = (IS_LEAP_YEAR(edate.year) && i >= 3);

		if (i == 12 || days - leap_sub < days_in_year[i + 1] + leap_day)
		{
			edate.month = i;
			days -= days_in_year[i] + leap_sub;
			break;
		}
	}
	edate.day = days + 1;

	etime.hour = (unix_time % SEC_IN_DAY) / SEC_IN_HOUR;
	etime.min = (unix_time % SEC_IN_HOUR) / SEC_IN_MIN;
	etime.twosec = (unix_time % SEC_IN_MIN) / 2;

	*date = cpu_to_le16(edate.raw);
	*time = cpu_to_le16(etime.raw);
}

void exfat_get_name(const struct exfat_node* node, char* buffer, size_t n)
{
	if (utf16_to_utf8(buffer, node->name, n, EXFAT_NAME_MAX) != 0)
		exfat_bug("failed to convert name to UTF-8");
}
