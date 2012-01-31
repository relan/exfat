/*
	utils.c (04.09.09)
	exFAT file system implementation library.

	Copyright (C) 2009, 2010  Andrew Nayenko

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "exfat.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#define _XOPEN_SOURCE /* for timezone in Linux */
#include <time.h>

void exfat_stat(const struct exfat* ef, const struct exfat_node* node,
		struct stat* stbuf)
{
	memset(stbuf, 0, sizeof(struct stat));
	if (node->flags & EXFAT_ATTRIB_DIR)
		stbuf->st_mode = S_IFDIR | (0777 & ~ef->dmask);
	else
		stbuf->st_mode = S_IFREG | (0777 & ~ef->fmask);
	stbuf->st_nlink = 1;
	stbuf->st_uid = ef->uid;
	stbuf->st_gid = ef->gid;
	stbuf->st_size = node->size;
	stbuf->st_blocks = DIV_ROUND_UP(node->size, CLUSTER_SIZE(*ef->sb)) *
		CLUSTER_SIZE(*ef->sb) / 512;
	stbuf->st_mtime = node->mtime;
	stbuf->st_atime = node->atime;
	/* set ctime to mtime to ensure we don't break programs that rely on ctime
	   (e.g. rsync) */
	stbuf->st_ctime = node->mtime;
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

time_t exfat_exfat2unix(le16_t date, le16_t time, uint8_t centisec)
{
	time_t unix_time = EPOCH_DIFF_SEC;
	uint16_t ndate = le16_to_cpu(date);
	uint16_t ntime = le16_to_cpu(time);

	uint16_t day    = ndate & 0x1f;      /* 5 bits, 1-31 */
	uint16_t month  = ndate >> 5 & 0xf;  /* 4 bits, 1-12 */
	uint16_t year   = ndate >> 9;        /* 7 bits, 1-127 (+1980) */

	uint16_t twosec = ntime & 0x1f;      /* 5 bits, 0-29 (2 sec granularity) */
	uint16_t min    = ntime >> 5 & 0xf;  /* 6 bits, 0-59 */
	uint16_t hour   = ntime >> 11;       /* 5 bits, 0-23 */

	if (day == 0 || month == 0 || month > 12)
	{
		exfat_error("bad date %hu-%02hu-%02hu",
				year + EXFAT_EPOCH_YEAR, month, day);
		return 0;
	}
	if (hour > 23 || min > 59 || twosec > 29)
	{
		exfat_error("bad time %hu:%02hu:%02hu",
				hour, min, twosec * 2);
		return 0;
	}
	if (centisec > 199)
	{
		exfat_error("bad centiseconds count %hhu", centisec);
		return 0;
	}

	/* every 4th year between 1904 and 2096 is leap */
	unix_time += year * SEC_IN_YEAR + LEAP_YEARS(year) * SEC_IN_DAY;
	unix_time += days_in_year[month] * SEC_IN_DAY;
	/* if it's leap year and February has passed we should add 1 day */
	if ((EXFAT_EPOCH_YEAR + year) % 4 == 0 && month > 2)
		unix_time += SEC_IN_DAY;
	unix_time += (day - 1) * SEC_IN_DAY;

	unix_time += hour * SEC_IN_HOUR;
	unix_time += min * SEC_IN_MIN;
	/* exFAT represents time with 2 sec granularity */
	unix_time += twosec * 2;
	unix_time += centisec / 100;

	/* exFAT stores timestamps in local time, so we correct it to UTC */
	unix_time += timezone;

	return unix_time;
}

void exfat_unix2exfat(time_t unix_time, le16_t* date, le16_t* time,
		uint8_t* centisec)
{
	time_t shift = EPOCH_DIFF_SEC + timezone;
	uint16_t day, month, year;
	uint16_t twosec, min, hour;
	int days;
	int i;

	/* time before exFAT epoch cannot be represented */
	if (unix_time < shift)
		unix_time = shift;

	unix_time -= shift;

	days = unix_time / SEC_IN_DAY;
	year = (4 * days) / (4 * 365 + 1);
	days -= year * 365 + LEAP_YEARS(year);
	month = 0;
	for (i = 1; i <= 12; i++)
	{
		int leap_day = (IS_LEAP_YEAR(year) && i == 2);
		int leap_sub = (IS_LEAP_YEAR(year) && i >= 3);

		if (i == 12 || days - leap_sub < days_in_year[i + 1] + leap_day)
		{
			month = i;
			days -= days_in_year[i] + leap_sub;
			break;
		}
	}
	day = days + 1;

	hour = (unix_time % SEC_IN_DAY) / SEC_IN_HOUR;
	min = (unix_time % SEC_IN_HOUR) / SEC_IN_MIN;
	twosec = (unix_time % SEC_IN_MIN) / 2;

	*date = cpu_to_le16(day | (month << 5) | (year << 9));
	*time = cpu_to_le16(twosec | (min << 5) | (hour << 11));
	if (centisec)
		*centisec = (unix_time % 2) * 100;
}

void exfat_get_name(const struct exfat_node* node, char* buffer, size_t n)
{
	if (utf16_to_utf8(buffer, node->name, n, EXFAT_NAME_MAX) != 0)
		exfat_bug("failed to convert name to UTF-8");
}

uint16_t exfat_start_checksum(const struct exfat_entry_meta1* entry)
{
	uint16_t sum = 0;
	int i;

	for (i = 0; i < sizeof(struct exfat_entry); i++)
		if (i != 2 && i != 3) /* skip checksum field itself */
			sum = ((sum << 15) | (sum >> 1)) + ((const uint8_t*) entry)[i];
	return sum;
}

uint16_t exfat_add_checksum(const void* entry, uint16_t sum)
{
	int i;

	for (i = 0; i < sizeof(struct exfat_entry); i++)
		sum = ((sum << 15) | (sum >> 1)) + ((const uint8_t*) entry)[i];
	return sum;
}

le16_t exfat_calc_checksum(const struct exfat_entry_meta1* meta1,
		const struct exfat_entry_meta2* meta2, const le16_t* name)
{
	uint16_t checksum;
	const int name_entries = DIV_ROUND_UP(utf16_length(name), EXFAT_ENAME_MAX);
	int i;

	checksum = exfat_start_checksum(meta1);
	checksum = exfat_add_checksum(meta2, checksum);
	for (i = 0; i < name_entries; i++)
	{
		struct exfat_entry_name name_entry = {EXFAT_ENTRY_FILE_NAME, 0};
		memcpy(name_entry.name, name + i * EXFAT_ENAME_MAX,
				EXFAT_ENAME_MAX * sizeof(le16_t));
		checksum = exfat_add_checksum(&name_entry, checksum);
	}
	return cpu_to_le16(checksum);
}

uint32_t exfat_vbr_start_checksum(const void* sector, size_t size)
{
	size_t i;
	uint32_t sum = 0;

	for (i = 0; i < size; i++)
		/* skip volume_state and allocated_percent fields */
		if (i != 0x6a && i != 0x6b && i != 0x70)
			sum = ((sum << 31) | (sum >> 1)) + ((const uint8_t*) sector)[i];
	return sum;
}

uint32_t exfat_vbr_add_checksum(const void* sector, size_t size, uint32_t sum)
{
	size_t i;

	for (i = 0; i < size; i++)
		sum = ((sum << 31) | (sum >> 1)) + ((const uint8_t*) sector)[i];
	return sum;
}


le16_t exfat_calc_name_hash(const struct exfat* ef, const le16_t* name)
{
	size_t i;
	size_t length = utf16_length(name);
	uint16_t hash = 0;

	for (i = 0; i < length; i++)
	{
		uint16_t c = le16_to_cpu(name[i]);

		/* convert to upper case */
		if (c < ef->upcase_chars)
			c = le16_to_cpu(ef->upcase[c]);

		hash = ((hash << 15) | (hash >> 1)) + (c & 0xff);
		hash = ((hash << 15) | (hash >> 1)) + (c >> 8);
	}
	return cpu_to_le16(hash);
}

void exfat_humanize_bytes(uint64_t value, struct exfat_human_bytes* hb)
{
	size_t i;
	const char* units[] = {"bytes", "KB", "MB", "GB", "TB", "PB"};
	uint64_t divisor = 1;
	uint64_t temp = 0;

	for (i = 0; i < sizeof(units) / sizeof(units[0]) - 1; i++, divisor *= 1024)
	{
		temp = (value + divisor / 2) / divisor;

		if (temp == 0)
			break;
		if (temp / 1024 * 1024 == temp)
			continue;
		if (temp < 10240)
			break;
	}
	hb->value = temp;
	hb->unit = units[i];
}

void exfat_print_info(const struct exfat_super_block* sb,
		uint32_t free_clusters)
{
	struct exfat_human_bytes hb;
	off_t total_space = le64_to_cpu(sb->sector_count) * SECTOR_SIZE(*sb);
	off_t avail_space = (off_t) free_clusters * CLUSTER_SIZE(*sb);

	printf("File system version           %hhu.%hhu\n",
			sb->version.major, sb->version.minor);
	exfat_humanize_bytes(SECTOR_SIZE(*sb), &hb);
	printf("Sector size          %10"PRIu64" %s\n", hb.value, hb.unit);
	exfat_humanize_bytes(CLUSTER_SIZE(*sb), &hb);
	printf("Cluster size         %10"PRIu64" %s\n", hb.value, hb.unit);
	exfat_humanize_bytes(total_space, &hb);
	printf("Volume size          %10"PRIu64" %s\n", hb.value, hb.unit);
	exfat_humanize_bytes(total_space - avail_space, &hb);
	printf("Used space           %10"PRIu64" %s\n", hb.value, hb.unit);
	exfat_humanize_bytes(avail_space, &hb);
	printf("Available space      %10"PRIu64" %s\n", hb.value, hb.unit);
}
