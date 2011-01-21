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
import platform
import SCons

env = Environment(**ARGUMENTS)

if not env['CCFLAGS']:
	if env['CC'] == 'gcc':
		env['CCFLAGS'] = '-Wall -O2 -ggdb'
env.Append(CPPDEFINES = {'FUSE_USE_VERSION': 26})
env.Append(CPPDEFINES = {'_FILE_OFFSET_BITS' : 64})
# __DARWIN_64_BIT_INO_T=0 define is needed because since Snow Leopard inode
# numbers are 64-bit by default, but libfuse operates 32-bit ones. This define
# forces 32-bit inode declaration in system headers, but it's also possible to
# link against libfuse_ino64 instead.
if platform.system() == 'Darwin':
	env.Append(CPPDEFINES = {'__DARWIN_64_BIT_INO_T' : 0})
	env.Append(CPPDEFINES = {'__DARWIN_UNIX03' : 1})
env.Append(CPPPATH = ['libexfat'])
env.Append(LINKFLAGS = '')

env.Library('libexfat/exfat', Glob('libexfat/*.c'))
mount = env.Program('fuse/mount.exfat-fuse', Glob('fuse/*.c'), LIBS = ['exfat', 'fuse'], LIBPATH = 'libexfat')
sbdump = env.Program('sbdump/sbdump', Glob('sbdump/*.c'), LIBS = ['exfat'], LIBPATH = 'libexfat')
fsck = env.Program('fsck/exfatfsck', Glob('fsck/*.c'), LIBS = ['exfat'], LIBPATH = 'libexfat')
mkfs = env.Program('mkfs/mkexfatfs', Glob('mkfs/*.c'), LIBS = ['exfat'], LIBPATH = 'libexfat')
label = env.Program('label/exfatlabel', Glob('label/*.c'), LIBS = ['exfat'], LIBPATH = 'libexfat')

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

Default([mount, sbdump, fsck, mkfs, label])
