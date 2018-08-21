#------------------------------------------------------------------------------
# Find microhttpd
# Find the microhttpd includes and library
# if you need to add a custom library search path, do it via via CMAKE_PREFIX_PATH
#
# This module defines
#  MHD_INCLUDE_DIRS, where to find header, etc.
#  MHD_LIBRARIES, the libraries needed to use jsoncpp.
#  MHD_FOUND, If false, do not try to use jsoncpp.
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
    MHD_INCLUDE_DIR
    NAMES microhttpd.h
    DOC "microhttpd include dir"
)

find_library(
    MHD_LIBRARY
    NAMES microhttpd microhttpd-10 libmicrohttpd libmicrohttpd-dll
    DOC "microhttpd library"
)

set(MHD_INCLUDE_DIRS ${MHD_INCLUDE_DIR})
set(MHD_LIBRARIES ${MHD_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(mhd DEFAULT_MSG
    MHD_LIBRARY MHD_INCLUDE_DIR)

mark_as_advanced(MHD_INCLUDE_DIR MHD_LIBRARY)