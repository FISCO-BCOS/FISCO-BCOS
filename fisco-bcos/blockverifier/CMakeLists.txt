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
 add_executable(test_verifier ${SRC_LIST} ${HEADERS})
 target_include_directories(test_verifier PRIVATE ${BOOST_INCLUDE_DIR})
 target_link_libraries(test_verifier devcrypto)
target_link_libraries(test_verifier ethcore)
target_link_libraries(test_verifier p2p)
target_link_libraries(test_verifier blockverifier)
target_link_libraries(test_verifier executivecontext)
target_link_libraries(test_verifier evm)
target_link_libraries(test_verifier sync)
target_link_libraries(test_verifier txpool)
target_link_libraries(test_verifier mptstate)
target_link_libraries(test_verifier blockchain)
target_link_libraries(test_verifier consensus)
target_link_libraries(test_verifier initializer)
target_link_libraries(test_verifier devcore)
 target_link_libraries(test_verifier Boost::program_options)
 if (UNIX)
target_link_libraries(test_verifier pthread)
endif()
 install(TARGETS test_verifier DESTINATION bin) 