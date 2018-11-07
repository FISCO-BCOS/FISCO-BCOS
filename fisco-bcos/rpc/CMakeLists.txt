#------------------------------------------------------------------------------
# Link libraries into main.cpp to generate executable binrary fisco-bcos
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

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")
include(EthDependencies)

add_executable(mini-rpc ${SRC_LIST} ${HEADERS})

target_include_directories(mini-rpc PRIVATE ..)
target_include_directories(mini-rpc PRIVATE ${BOOST_INCLUDE_DIR})
target_link_libraries(mini-rpc Boost::program_options)
target_link_libraries(mini-rpc Boost::UnitTestFramework)
target_link_libraries(mini-rpc rpc)
target_link_libraries(mini-rpc ledger)
target_link_libraries(mini-rpc p2p)
target_link_libraries(mini-rpc JsonRpcCpp::Server ${MHD_LIBRARY})

if (UNIX)
target_link_libraries(mini-rpc pthread)
endif()

install(TARGETS mini-rpc DESTINATION bin)
