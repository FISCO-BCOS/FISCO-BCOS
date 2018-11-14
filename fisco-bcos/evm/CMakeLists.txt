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

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

include(EthDependencies)

add_executable(mini-evm ${SRC_LIST} ${HEADERS})

target_include_directories(mini-evm PRIVATE ${BOOST_INCLUDE_DIR})

target_link_libraries(mini-evm devcore)
target_link_libraries(mini-evm devcrypto)
target_link_libraries(mini-evm ethcore)
target_link_libraries(mini-evm evm)
target_link_libraries(mini-evm executivecontext)
target_link_libraries(mini-evm mptstate)

install(TARGETS mini-evm DESTINATION bin)
