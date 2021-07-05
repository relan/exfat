#
#	Makefile.am (08.01.20)
#	Automake source.
#
#	Free exFAT implementation.
#	Copyright (C) 2011-2018  Andrew Nayenko
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

sbin_PROGRAMS = resizeexfatfs
# dist_man8_MANS = resizeexfatfs.8
resizeexfatfs_SOURCES = main.c method_2.c
resizeexfatfs_CPPFLAGS = -I$(top_srcdir)/libexfat
resizeexfatfs_LDADD = ../libexfat/libexfat.a
