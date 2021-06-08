#------------------------------------------------------------------------------
# Top-level CMake file for FISCO-BCOS.
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

# generate executable binary fisco-bcos
add_subdirectory(main)

if (DEMO)
    add_subdirectory(para)
    add_subdirectory(p2p)
    add_subdirectory(consensus)
    add_subdirectory(blockverifier)
    add_subdirectory(sync)
    add_subdirectory(evm)
    add_subdirectory(rpc)
    if(USE_HSM_SDF)
    add_subdirectory(crypto)
    endif()
endif()

if (TOOL)
    add_subdirectory(datasync)
    add_subdirectory(benchmark)
    add_subdirectory(tools)
endif()

