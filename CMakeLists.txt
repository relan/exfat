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

cmake_minimum_required(VERSION 3.1)
project(exfat
  VERSION 1.2.7
  DESCRIPTION "Free exFAT implementation"
  LANGUAGES C
)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "None" "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_C_STANDARD 99)
add_compile_options(-Wall -Werror)

find_package(FUSE REQUIRED)

# FUSE requires 64-bit off_t and FUSE2 unconditionally expects this macro.
set(_FILE_OFFSET_BITS 64)

configure_file(libexfat/cmakeconfig.h.in libexfat/config.h)

include(GNUInstallDirs)
function(install_symlink target link_name)
  get_filename_component(_dir "${link_name}" DIRECTORY)
  get_filename_component(_name "${link_name}" NAME)
  set(temp_link "${CMAKE_CURRENT_BINARY_DIR}/${_name}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink "${target}" "${temp_link}")
  install(FILES "${temp_link}" DESTINATION "${_dir}" RENAME "${_name}")
endfunction()

add_subdirectory(libexfat)
add_subdirectory(dump)
add_subdirectory(fsck)
add_subdirectory(fuse)
add_subdirectory(label)
add_subdirectory(mkfs)
