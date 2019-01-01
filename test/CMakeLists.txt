#------------------------------------------------------------------------------
# Enable Unit Test for FISCO-BCOS.
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
file(GLOB_RECURSE sources "*.cpp" "*.h" "*.sol")
set(TEST_ARGS "--testpath=${CMAKE_SOURCE_DIR}/test/data")
foreach(file ${sources})
    file(STRINGS ${file} test_list_raw REGEX "BOOST_.*TEST_(SUITE|CASE|SUITE_END)")
    set(TestSuite "DEFAULT")
    set(TestSuitePath "")
    foreach(test_raw ${test_list_raw})
        string(REGEX REPLACE ".*TEST_(SUITE|CASE)\\(([^ ,\\)]*).*" "\\1 \\2" test ${test_raw})

        #skip disabled
        if (";${excludeSuites};" MATCHES ";${TestSuite};")
            continue()
        endif()

        if(test MATCHES "^SUITE .*")

            string(SUBSTRING ${test} 6 -1 TestSuite)
            set(TestSuitePath "${TestSuitePath}/${TestSuite}")

            if(FASTCTEST)
                if (";${excludeSuites};" MATCHES ";${TestSuite};")
                    continue()
                endif()
                if (NOT ";${allSuites};" MATCHES ";${TestSuite};")
                    string(SUBSTRING ${TestSuitePath} 1 -1 TestSuitePathFixed)
                    list(APPEND allSuites ${TestSuite})
                    separate_arguments(TEST_ARGS)
                    set(TestArgs -t ${TestSuitePathFixed} -- ${TEST_ARGS})
                    add_test(NAME ${TestSuitePathFixed} WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND ./bin/test-fisco-bcos ${TestArgs})
                endif()
            endif()
        elseif(test MATCHES "^CASE .*")
            if(NOT FASTCTEST)
                if(NOT test MATCHES "^CASE &createRandom.*")
                    string(SUBSTRING ${test} 5 -1 TestCase)
                    string(SUBSTRING ${TestSuitePath} 1 -1 TestSuitePathFixed)
                    separate_arguments(TEST_ARGS)
                    set(TestArgs -t ${TestSuitePathFixed}/${TestCase} -- ${TEST_ARGS})
                    add_test(NAME ${TestSuitePathFixed}/${TestCase} WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND ./bin/test-fisco-bcos ${TestArgs})
                endif()
            endif()
        elseif (";${test_raw};" MATCHES "BOOST_AUTO_TEST_SUITE_END()")
            #encountered SUITE_END block. remove one test suite from the suite path.
            string(FIND ${TestSuitePath} "/" Position REVERSE)
            string(SUBSTRING ${TestSuitePath} 0 ${Position} TestSuitePath)
        endif()
    endforeach(test_raw)
endforeach(file)

include(ProcessorCount)
ProcessorCount(N)
if(NOT N EQUAL 0)
  set(CTEST_BUILD_FLAGS -j${N})
  set(ctest_test_args ${ctest_test_args} PARALLEL_LEVEL ${N})
endif()

if(NOT EASYLOG)
    list(REMOVE_ITEM sources ${CMAKE_CURRENT_SOURCE_DIR}/unittests/libdevcore/easylogging++.cpp)
endif()
add_executable(test-fisco-bcos ${sources})

target_include_directories(test-fisco-bcos PRIVATE ..)
target_include_directories(test-fisco-bcos PRIVATE ../utils)
target_link_libraries(test-fisco-bcos Boost::UnitTestFramework)
target_link_libraries(test-fisco-bcos Boost::Filesystem)
target_link_libraries(test-fisco-bcos Boost::Thread)
target_link_libraries(test-fisco-bcos devcore)
target_link_libraries(test-fisco-bcos devcrypto)
target_link_libraries(test-fisco-bcos ethcore)
target_link_libraries(test-fisco-bcos evm)
target_link_libraries(test-fisco-bcos evmc)
target_link_libraries(test-fisco-bcos interpreter)
target_link_libraries(test-fisco-bcos network)
target_link_libraries(test-fisco-bcos mptstate)
target_link_libraries(test-fisco-bcos storagestate)
target_link_libraries(test-fisco-bcos executivecontext)
target_link_libraries(test-fisco-bcos txpool)
target_link_libraries(test-fisco-bcos blockchain)
target_link_libraries(test-fisco-bcos blockverifier)
target_link_libraries(test-fisco-bcos consensus)
target_link_libraries(test-fisco-bcos sync)
target_link_libraries(test-fisco-bcos initializer)
target_link_libraries(test-fisco-bcos ledger)
target_link_libraries(test-fisco-bcos rpc)

enable_testing()
set(CTEST_OUTPUT_ON_FAILURE TRUE)
