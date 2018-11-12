#------------------------------------------------------------------------------
# Find the leveldb includes and library
# 
# if you need to add a custom library search path, do it via via CMAKE_PREFIX_PATH 
# 
# This module defines
#  LEVELDB_INCLUDE_DIRS, where to find header, etc.
#  LEVELDB_LIBRARIES, the libraries needed to use leveldb.
#  LEVELDB_FOUND, If false, do not try to use leveldb.
# ------------------------------------------------------------------------------
# This file is part of FISCO-BCOS.
#
# FISCO-BCOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# FISCO-BCOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
#
# (c) 2016-2018 fisco-dev contributors.
#------------------------------------------------------------------------------
find_path(
    LEVELDB_INCLUDE_DIR 
    NAMES leveldb/db.h
    DOC "leveldb include dir"
)

find_library(
    LEVELDB_LIBRARY
    NAMES leveldb
    DOC "leveldb library"
)

set(LEVELDB_INCLUDE_DIRS ${LEVELDB_INCLUDE_DIR})
set(LEVELDB_LIBRARIES ${LEVELDB_LIBRARY})
if (STATIC_BUILD)
	find_path(SNAPPY_INCLUDE_DIR snappy.h PATH_SUFFIXES snappy)
	find_library(SNAPPY_LIBRARY snappy)
	set(LEVELDB_INCLUDE_DIRS ${LEVELDB_INCLUDE_DIR} ${SNAPPY_INCLUDE_DIR})
	set(LEVELDB_LIBRARIES ${LEVELDB_LIBRARY} ${SNAPPY_LIBRARY})
endif()
# handle the QUIETLY and REQUIRED arguments and set LEVELDB_FOUND to TRUE
# if all listed variables are TRUE, hide their existence from configuration view
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(leveldb DEFAULT_MSG
    LEVELDB_LIBRARY LEVELDB_INCLUDE_DIR)
mark_as_advanced (LEVELDB_INCLUDE_DIR LEVELDB_LIBRARY)
