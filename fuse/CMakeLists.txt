#
# CMakeLists.txt (29.12.17)
# CMake build system
#
# Free exFAT implementation.
# Copyright (C) 2010-2017  Andrew Nayenko
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

add_executable(mount.exfat-fuse main.c)
target_include_directories(mount.exfat-fuse PRIVATE
  ${FUSE_INCLUDE_DIRS}
)
target_link_libraries(mount.exfat-fuse
  libexfat
  ${FUSE_LIBRARIES}
)

install(TARGETS mount.exfat-fuse DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(FILES mount.exfat-fuse.8 DESTINATION ${CMAKE_INSTALL_MANDIR}/man8)
install_symlink(mount.exfat-fuse ${CMAKE_INSTALL_SBINDIR}/mount.exfat)
