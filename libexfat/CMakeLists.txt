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

add_library(libexfat STATIC
  cluster.c
  io.c
  log.c
  lookup.c
  mount.c
  node.c
  time.c
  utf.c
  utils.c
)

# Allow dependencies to find our headers and config.h
target_include_directories(libexfat PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)
