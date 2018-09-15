#
#	configure.ac (30.03.15)
#	Autoconf source.
#
#	Free exFAT implementation.
#	Copyright (C) 2010-2018  Andrew Nayenko
#
#	This program is free software; you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 2 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License along
#	with this program; if not, write to the Free Software Foundation, Inc.,
#	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

AC_INIT([Free exFAT implementation],
	[1.3.0],
	[relan@users.noreply.github.com],
	[exfat],
	[https://github.com/relan/exfat])
AM_INIT_AUTOMAKE([1.11.2 -Wall -Werror foreign subdir-objects])
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_RANLIB
AM_PROG_AR
AC_SYS_LARGEFILE
PKG_CHECK_MODULES([UBLIO], [libublio], [
  CFLAGS="$CFLAGS $UBLIO_CFLAGS"
  LIBS="$LIBS $UBLIO_LIBS"
  AC_DEFINE([USE_UBLIO], [1],
    [Define if block devices are not supported.])
], [:])
PKG_CHECK_MODULES([FUSE], [fuse])
AC_CONFIG_HEADERS([libexfat/config.h])
AC_CONFIG_FILES([
	libexfat/Makefile
	dump/Makefile
	fsck/Makefile
	fuse/Makefile
	label/Makefile
	mkfs/Makefile
	Makefile])
AC_OUTPUT
