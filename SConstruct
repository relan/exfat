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
conf = Configure(env)

destdir = env.get('DESTDIR', '/sbin');
targets = []
libs = ['exfat']

if 'CC' in os.environ:
	conf.env.Replace(CC = os.environ['CC'])
if 'CCFLAGS' in os.environ:
	conf.env.Replace(CCFLAGS = os.environ['CCFLAGS'])
# Set default CCFLAGS for known compilers
if not conf.env['CCFLAGS']:
	if conf.env['CC'] == 'gcc':
		conf.env.Replace(CCFLAGS = '-Wall -O2 -ggdb')
	elif conf.env['CC'] == 'clang':
		conf.env.Replace(CCFLAGS = '-Wall -O2 -g')
conf.env.Append(CPPDEFINES = {'FUSE_USE_VERSION': 26})
conf.env.Append(CPPDEFINES = {'_FILE_OFFSET_BITS' : 64})
conf.env.Append(CPPPATH = ['libexfat'])
if 'LDFLAGS' in os.environ:
	conf.env.Append(LINKFLAGS = os.environ['LDFLAGS'])
conf.env.Append(LIBPATH = ['libexfat'])

# __DARWIN_64_BIT_INO_T=0 define is needed because since Snow Leopard inode
# numbers are 64-bit by default, but libfuse operates 32-bit ones. This define
# forces 32-bit inode declaration in system headers, but it's also possible to
# link against libfuse_ino64 instead.
if platform.system() == 'Darwin':
	conf.env.Append(CPPDEFINES = {'__DARWIN_64_BIT_INO_T' : 0})
	conf.env.Append(CPPDEFINES = {'__DARWIN_UNIX03' : 1})

# FreeBSD does not support block devices, only raw devices. Ublio is required
# for unaligned I/O and caching.
if platform.system() == 'FreeBSD':
	conf.env.Append(CPPDEFINES = 'USE_UBLIO')
	libs.append('ublio')
	conf.env.Append(CPPPATH = ['/usr/local/include'])
	conf.env.Append(LIBPATH = ['/usr/local/lib'])

env = conf.Finish()

def make_symlink(dir, target, link_name):
	workdir = os.getcwd()
	os.chdir(dir)
	try:
		os.remove(link_name)
	except OSError:
		pass
	os.symlink(target, link_name)
	os.chdir(workdir)

symlink = SCons.Action.ActionFactory(make_symlink,
		lambda dir, target, link_name:
				'make_symlink("%s", "%s", "%s")' % (dir, target, link_name))

def program(pattern, output, alias, libs):
	sources = Glob(pattern)
	if not sources:
		return
	target = env.Program(output, sources, LIBS = libs)
	if alias:
		Alias('install', Install(destdir, target),
				symlink(destdir, os.path.basename(output), alias))
	else:
		Alias('install', Install(destdir, target))
	targets.append(target)

env.Library('libexfat/exfat', Glob('libexfat/*.c'))

program('fuse/*.c', 'fuse/mount.exfat-fuse', 'mount.exfat', [libs + ['fuse']])
program('dump/*.c', 'dump/dumpexfat', None, libs)
program('fsck/*.c', 'fsck/exfatfsck', 'fsck.exfat', libs)
program('mkfs/*.c', 'mkfs/mkexfatfs', 'mkfs.exfat', libs)
program('label/*.c', 'label/exfatlabel', None, libs)

Default(targets)
