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
# File: CompilerSettings.cmake
# Function: Common cmake file for setting compilation environment variables
# ------------------------------------------------------------------------------

#add_definitions(-Wno-unused-value -Wunused-parameter)

set(CMAKE_CXX_STANDARD 20)
message(STATUS "COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
if(("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU") OR("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
    find_program(CCACHE_PROGRAM ccache)

    if(CCACHE_PROGRAM)
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
        set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CCACHE_PROGRAM}")
    endif()

    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    # add_compile_options(-Werror)
    add_compile_options(-Wall)
    add_compile_options(-pedantic)
    add_compile_options(-Wextra)
    add_compile_options(-Wno-unknown-pragmas)
    add_compile_options(-fno-omit-frame-pointer)
    add_compile_options(-fvisibility=hidden)
    add_compile_options(-fvisibility-inlines-hidden)
    add_compile_options(-Wno-unused-variable)

    # for boost json spirit
    add_compile_options(-DBOOST_SPIRIT_THREADSAFE)

    # for tbb, TODO: https://software.intel.com/sites/default/files/managed/b2/d2/TBBRevamp.pdf
    add_compile_options(-DTBB_SUPPRESS_DEPRECATED_MESSAGES=1)

    # build deps lib Release
    set(_only_release_configuration "-DCMAKE_BUILD_TYPE=Release")

    if(BUILD_STATIC)
        SET(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
        SET(BUILD_SHARED_LIBRARIES OFF)
        SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

        # Note: If bring the -static option, apple will fail to link
        if(NOT APPLE)
            SET(CMAKE_EXE_LINKER_FLAGS "-static")
        endif()

        # SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Bdynamic -ldl -lpthread -Wl,-Bstatic -static-libstdc++ ")
    endif()

    if(TESTS)
        add_compile_options(-DBOOST_TEST_THREAD_SAFE)
    endif()

    if(PROF)
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
        SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
        SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -pg")
    endif()

    # Configuration-specific compiler settings.
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

    option(USE_LD_GOLD "Use GNU gold linker" ON)

    if(USE_LD_GOLD)
        execute_process(COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)

        if("${LD_VERSION}" MATCHES "GNU gold")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=gold")
        endif()
    endif()

    if("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
        # Check that we've got GCC 7.0 or newer.
        set(GCC_MIN_VERSION "10.0")
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)

        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${MARCH_TYPE}")
        set(CMAKE_C_FLAGS "-std=c99 ${CMAKE_C_FLAGS} ${MARCH_TYPE}")

        add_compile_options(-fstack-protector-strong)
        add_compile_options(-fstack-protector)
        add_compile_options(-fPIC)
        add_definitions(-DUSE_STD_RANGES)
    elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.0)
            set(CMAKE_CXX_FLAGS_DEBUG "-O -g")
        endif()

        add_compile_options(-fstack-protector)
        add_compile_options(-Winconsistent-missing-override)

        # Some Linux-specific Clang settings.  We don't want these for OS X.
        if("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
            # Tell Boost that we're using Clang's libc++.   Not sure exactly why we need to do.
            add_definitions(-DBOOST_ASIO_HAS_CLANG_LIBCXX)

            # Use fancy colors in the compiler diagnostics
            add_compile_options(-fcolor-diagnostics)
        endif()
    endif()

    if(SANITIZE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=address -fsanitize=leak -fsanitize-recover=all")

        if(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize-blacklist=${CMAKE_SOURCE_DIR}/sanitizer-blacklist.txt")
        endif()
    endif()


    if(COVERAGE)
        set(TESTS ON)

        if("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
            set(CMAKE_CXX_FLAGS "-g --coverage ${CMAKE_CXX_FLAGS}")
            set(CMAKE_C_FLAGS "-g --coverage ${CMAKE_C_FLAGS}")
            set(CMAKE_SHARED_LINKER_FLAGS "--coverage ${CMAKE_SHARED_LINKER_FLAGS}")
            set(CMAKE_EXE_LINKER_FLAGS "--coverage ${CMAKE_EXE_LINKER_FLAGS}")
        elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
            add_compile_options(-Wno-unused-command-line-argument)
            set(CMAKE_CXX_FLAGS "-g -fprofile-arcs -ftest-coverage ${CMAKE_CXX_FLAGS}")
            set(CMAKE_C_FLAGS "-g -fprofile-arcs -ftest-coverage ${CMAKE_C_FLAGS}")
        endif()
    endif()
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    add_definitions(-DUSE_STD_RANGES)
    add_compile_options(/std:c++latest)
    add_compile_options(-bigobj)

    # MSVC only support static build
    set(CMAKE_CXX_FLAGS_DEBUG "/MTd /DEBUG")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "/MT /Os")
    set(CMAKE_CXX_FLAGS_RELEASE "/MT")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MT /DEBUG")
    link_libraries(ws2_32 Crypt32 userenv)
else()
    message(WARNING "Your compiler is not tested, if you run into any issues, we'd welcome any patches.")
endif()

# rust static library linking requirements for macos
if(APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Security")
else()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-as-needed -ldl")
endif()

set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY ON)
