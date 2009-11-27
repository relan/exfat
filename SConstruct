#
#  SConstruct
#  SConscript for all components.
#
#  Created by Andrew Nayenko on 10.09.09.
#  This software is distributed under the GNU General Public License 
#  version 3 or any later.
#

import os

# Define __DARWIN_64_BIT_INO_T=0 is needed for Snow Leopard support because
# in it's headers inode numbers are 64-bit by default, but libfuse operates
# 32-bit inode numbers. It's also possible to link against libfuse_ino64
# instead.
cflags = '-Wall -O2 -ggdb -D_FILE_OFFSET_BITS=64 -D__DARWIN_64_BIT_INO_T=0 -Ilibexfat'
ldflags = ''

Library('libexfat/exfat', Glob('libexfat/*.c'), CFLAGS = cflags, LINKFLAGS = ldflags)
fsck = Program('fsck/exfatck', Glob('fsck/*.c'), CFLAGS = cflags, LINKFLAGS = ldflags, LIBS = ['exfat'], LIBPATH = 'libexfat')
mount = Program('fuse/mount.exfat-fuse', Glob('fuse/*.c'), CFLAGS = cflags + ' -DFUSE_USE_VERSION=26', LINKFLAGS = ldflags, LIBS = ['exfat', 'fuse'], LIBPATH = 'libexfat')

try:
	destdir = os.environ['DESTDIR']
except:
	destdir = '/sbin'
Alias('install', Install(dir = destdir, source = mount))

Default([mount, fsck])
