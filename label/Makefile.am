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

sbin_PROGRAMS = exfatlabel
dist_man8_MANS = exfatlabel.8
exfatlabel_SOURCES = main.c
exfatlabel_CPPFLAGS = -imacros $(top_srcdir)/libexfat/config.h
exfatlabel_CFLAGS = $(FUSE2_CFLAGS) $(FUSE3_CFLAGS) $(UBLIO_CFLAGS)
exfatlabel_LDADD = $(top_srcdir)/libexfat/libexfat.a $(UBLIO_LIBS)
