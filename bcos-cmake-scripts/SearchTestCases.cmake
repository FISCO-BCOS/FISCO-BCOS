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
function(config_test_cases TEST_ARGS SOURCES TEST_BINARY_PATH EXCLUDE_SUITES)
    foreach(file ${SOURCES})
        file(STRINGS ${file} test_list_raw REGEX "BOOST_.*TEST_(SUITE|CASE|SUITE_END)")
        set(TestSuite "DEFAULT")
        set(TestSuitePath "")
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
                        string(SUBSTRING ${TestSuitePath} 1 -1 TestSuitePathFixed)
                        list(APPEND allSuites ${TestSuite})
                        separate_arguments(TEST_ARGS)
                        set(TestArgs -t ${TestSuitePathFixed} -- ${TEST_ARGS})
                        add_test(NAME ${TestSuitePathFixed} WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND ${TEST_BINARY_PATH} ${TestArgs})
                    endif()
                endif()
            elseif(test MATCHES "^CASE .*")
                if(NOT FASTCTEST)
                    if(NOT test MATCHES "^CASE &createRandom.*")
                        string(SUBSTRING ${test} 5 -1 TestCase)
                        string(SUBSTRING ${TestSuitePath} 1 -1 TestSuitePathFixed)
                        separate_arguments(TEST_ARGS)
                        set(TestArgs -t ${TestSuitePathFixed}/${TestCase} -- ${TEST_ARGS})
                        add_test(NAME ${TestSuitePathFixed}/${TestCase} WORKING_DIRECTORY ${CMAKE_BINARY_DIR} COMMAND ${TEST_BINARY_PATH} ${TestArgs})
                    endif()
                endif()
            elseif (";${test_raw};" MATCHES "BOOST_AUTO_TEST_SUITE_END()")
                #encountered SUITE_END block. remove one test suite from the suite path.
                string(FIND ${TestSuitePath} "/" Position REVERSE)
                string(SUBSTRING ${TestSuitePath} 0 ${Position} TestSuitePath)
            endif()
        endforeach(test_raw)
    endforeach(file)
endfunction()
