#
#	Makefile.am (30.03.15)
#	Automake source.
#
#	Free exFAT implementation.
#	Copyright (C) 2011-2023  Andrew Nayenko
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

sbin_PROGRAMS = dumpexfat
dist_man8_MANS = dumpexfat.8
dumpexfat_SOURCES = main.c
dumpexfat_CPPFLAGS = -imacros $(top_srcdir)/libexfat/config.h
dumpexfat_CFLAGS = $(FUSE2_CFLAGS) $(FUSE3_CFLAGS) $(UBLIO_CFLAGS)
dumpexfat_LDADD = $(top_srcdir)/libexfat/libexfat.a $(UBLIO_LIBS)
