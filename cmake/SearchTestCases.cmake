# ------------------------------------------------------------------------------
# Copyright (C) 2021 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ------------------------------------------------------------------------------
# File: SearchTestCases.cmake
# Function: cmake to help search test cases
# ------------------------------------------------------------------------------
function(config_test_cases TEST_ARGS SOURCES TEST_BINARY_PATH EXCLUDE_SUITES WORKING_DIRECTORY)
    foreach(file ${SOURCES})
        file(STRINGS ${file} test_list_raw REGEX "BOOST_.*TEST_(SUITE|CASE|SUITE_END)")
        set(TestSuite "DEFAULT")
        set(TestSuitePath "")
        # Set working directory variable
        if(WORKING_DIRECTORY STREQUAL "")
            set(_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
        else()
            set(_WORKING_DIRECTORY "${WORKING_DIRECTORY}")
        endif()
        foreach(test_raw ${test_list_raw})
            string(REGEX REPLACE ".*TEST_(SUITE|CASE)\\(([^ ,\\)]*).*" "\\1 \\2" test ${test_raw})

            #skip disabled
            if (";${EXCLUDE_SUITES};" MATCHES ";${TestSuite};")
                continue()
            endif()

            if(test MATCHES "^SUITE .*")
                string(SUBSTRING ${test} 6 -1 TestSuite)
                set(TestSuitePath "${TestSuitePath}/${TestSuite}")

                if(FASTCTEST)
                    if (";${EXCLUDE_SUITES};" MATCHES ";${TestSuite};")
                        continue()
                    endif()
                    if (NOT ";${allSuites};" MATCHES ";${TestSuite};")
                        if(NOT TestSuitePath STREQUAL "")
                            string(SUBSTRING ${TestSuitePath} 1 -1 TestSuitePathFixed)
                        else()
                            set(TestSuitePathFixed "")
                        endif()
                        list(APPEND allSuites ${TestSuite})
                        set(_TestArgs ${TEST_ARGS})
                        separate_arguments(_TestArgs)
                        set(TestName ${TestSuitePathFixed})
                        set(TestArgs -t ${TestSuitePathFixed} -- ${_TestArgs})
                        add_test(NAME ${TestName} WORKING_DIRECTORY ${_WORKING_DIRECTORY} COMMAND ${TEST_BINARY_PATH} ${TestArgs})
                        set_tests_properties(${TestName} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:detect_container_overflow=0")
                    endif()
                endif()
            elseif(test MATCHES "^CASE .*")
                if(NOT FASTCTEST)
                    if(NOT test MATCHES "^CASE &createRandom.*")
                        string(SUBSTRING ${test} 5 -1 TestCase)
                        if(NOT TestSuitePath STREQUAL "")
                            string(SUBSTRING ${TestSuitePath} 1 -1 TestSuitePathFixed)
                        else()
                            set(TestSuitePathFixed "")
                        endif()
                        set(_TestArgs ${TEST_ARGS})
                        separate_arguments(_TestArgs)
                        set(TestName "${TestSuitePathFixed}/${TestCase}")
                        set(TestArgs -t ${TestSuitePathFixed}/${TestCase} -- ${_TestArgs})
                        add_test(NAME ${TestName} WORKING_DIRECTORY ${_WORKING_DIRECTORY} COMMAND ${TEST_BINARY_PATH} ${TestArgs})
                        set_tests_properties(${TestName} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:detect_container_overflow=0")
                    endif()
                endif()
            elseif (";${test_raw};" MATCHES "BOOST_AUTO_TEST_SUITE_END()")
                #encountered SUITE_END block. remove one test suite from the suite path.
                string(FIND ${TestSuitePath} "/" Position REVERSE)
                if(Position GREATER 0)
                    string(SUBSTRING ${TestSuitePath} 0 ${Position} TestSuitePath)
                else()
                    set(TestSuitePath "")
                endif()
            endif()
        endforeach(test_raw)
    endforeach(file)
endfunction()
