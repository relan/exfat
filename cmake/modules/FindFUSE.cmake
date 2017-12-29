find_package(PkgConfig QUIET)
pkg_check_modules(PC_FUSE QUIET fuse)

find_path(FUSE_INCLUDE_DIR
  NAMES fuse.h
  HINTS ${PC_FUSE_INCLUDE_DIRS}
)
find_library(FUSE_LIBRARY
  NAMES fuse
  HINTS ${PC_FUSE_LIBRARY_DIRS}
)

if(PC_FUSE_FOUND)
  set(FUSE_VERSION "${PC_FUSE_VERSION}")
elseif(FUSE_INCLUDE_DIR AND EXISTS "${FUSE_INCLUDE_DIR}/fuse_common.h")
  set(_major_version_regex "#define[ \t]+FUSE_MAJOR_VERSION[ \t]+([0-9]+).*")
  set(_minor_version_regex "#define[ \t]+FUSE_MINOR_VERSION[ \t]+([0-9]+).*")
  file(STRINGS "${FUSE_INCLUDE_DIR}/fuse_common.h"
    _fuse_version REGEX "^(${_major_version_regex}|${_minor_version_regex})")
  string(REGEX REPLACE ".*${_major_version_regex}" "\\1"
    _fuse_major_version "${_fuse_version}")
  string(REGEX REPLACE ".*${_minor_version_regex}" "\\1"
    _fuse_minor_version "${_fuse_version}")
  set(FUSE_VERSION "${_fuse_major_version}.${_fuse_minor_version}")
endif()

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set FUSE_FOUND to TRUE
# if all listed variables are TRUE and the requested version matches.
find_package_handle_standard_args(FUSE REQUIRED_VARS
                                  FUSE_LIBRARY FUSE_INCLUDE_DIR
                                  VERSION_VAR FUSE_VERSION)

if(FUSE_FOUND)
  set(FUSE_LIBRARIES     ${FUSE_LIBRARY})
  set(FUSE_INCLUDE_DIRS  ${FUSE_INCLUDE_DIR})
endif()
mark_as_advanced(FUSE_INCLUDE_DIR FUSE_LIBRARY)
