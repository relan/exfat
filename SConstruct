#
#	SConstruct (10.09.09)
#	SConscript for all components.
#
#	Copyright (C) 2009, 2010  Andrew Nayenko
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import os
import SCons

# Define __DARWIN_64_BIT_INO_T=0 is needed for Snow Leopard support because
# in it's headers inode numbers are 64-bit by default, but libfuse operates
# 32-bit inode numbers. It's also possible to link against libfuse_ino64
# instead.
cflags = '-Wall -O2 -ggdb -D_FILE_OFFSET_BITS=64 -D__DARWIN_64_BIT_INO_T=0 -Ilibexfat'
ldflags = ''

Library('libexfat/exfat', Glob('libexfat/*.c'), CFLAGS = cflags, LINKFLAGS = ldflags)
fsck = Program('fsck/exfatck', Glob('fsck/*.c'), CFLAGS = cflags, LINKFLAGS = ldflags, LIBS = ['exfat'], LIBPATH = 'libexfat')
mount = Program('fuse/mount.exfat-fuse', Glob('fuse/*.c'), CFLAGS = cflags + ' -DFUSE_USE_VERSION=26', LINKFLAGS = ldflags, LIBS = ['exfat', 'fuse'], LIBPATH = 'libexfat')

def get_destdir():
	try:
		destdir = os.environ['DESTDIR']
	except KeyError:
		destdir = '/sbin'
	return destdir

def make_symlink((dir)):
	workdir = os.getcwd()
	os.chdir(dir)
	try:
		os.remove('mount.exfat')
	except OSError:
		pass
	os.symlink('mount.exfat-fuse', 'mount.exfat')
	os.chdir(workdir)

symlink = SCons.Action.ActionFactory(make_symlink,
		lambda dir: 'make_symlink("%s")' % dir)
Alias('install',
		Install(dir = get_destdir(), source = mount),
		symlink(dir = get_destdir()))

Default([mount, fsck])
