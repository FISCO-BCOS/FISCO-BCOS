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

aux_source_directory(. SRC_LIST)

file(GLOB HEADERS "*.h")

include(EthDependencies)

add_executable(mini-consensus ${SRC_LIST} ${HEADERS})

target_include_directories(mini-consensus PRIVATE ${BOOST_INCLUDE_DIR})
target_link_libraries(mini-consensus Boost::program_options)

target_link_libraries(mini-consensus devcore)
target_link_libraries(mini-consensus devcrypto)
target_link_libraries(mini-consensus ethcore)
target_link_libraries(mini-consensus p2p)
target_link_libraries(mini-consensus blockverifier)
target_link_libraries(mini-consensus sync)
target_link_libraries(mini-consensus txpool)
target_link_libraries(mini-consensus blockchain)
target_link_libraries(mini-consensus consensus)
target_link_libraries(mini-consensus ledger)

if (UNIX)
target_link_libraries(mini-consensus pthread)
endif()

install(TARGETS mini-consensus DESTINATION bin)
