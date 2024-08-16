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
set(Boost_NO_WARN_NEW_VERSIONS ON)
message(STATUS "COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
if(("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU") OR("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
    find_program(CCACHE_PROGRAM ccache)

    if(CCACHE_PROGRAM)
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
        set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CCACHE_PROGRAM}")
    endif()

    add_compile_options(-Werror)
    add_compile_options(-Wall)
    add_compile_options(-pedantic)
    add_compile_options(-Wextra)

    # Ignore warnings
    add_compile_options(-Wno-unused-parameter)
    add_compile_options(-Wno-unused-variable)
    add_compile_options(-Wno-error=unknown-pragmas)
    add_compile_options(-Wno-error=deprecated-declarations)
    add_compile_options(-fno-omit-frame-pointer)
    add_compile_options(-Wno-error=strict-aliasing)

    if(NOT APPLE)
        set(CMAKE_CXX_VISIBILITY_PRESET hidden)
        add_compile_options(-fvisibility=hidden)
        add_compile_options(-fvisibility-inlines-hidden)
    endif()

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
            SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
        endif()

        # SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Bdynamic -ldl -lpthread -Wl,-Bstatic")
    endif()

    if(TESTS)
        add_compile_options(-DBOOST_TEST_THREAD_SAFE)
    endif()

    # Configuration-specific compiler settings.
    set(CMAKE_CXX_FLAGS_DEBUG "-Og -g")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")
    if(ONLY_CPP_SDK)
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    else ()
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -DNDEBUG")
    endif ()
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

    if("${LINKER}" MATCHES "gold")
        execute_process(COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
        if("${LD_VERSION}" MATCHES "GNU gold")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=gold")
        endif()
    elseif("${LINKER}" MATCHES "mold")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=mold")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=mold")
    endif()

    if("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
        # Check that we've got GCC 10.0 or newer.
        set(GCC_MIN_VERSION "10.0")
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)

        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${MARCH_TYPE}")
        set(CMAKE_C_FLAGS "-std=c99 -fexceptions ${CMAKE_C_FLAGS} ${MARCH_TYPE}")

        add_compile_options(-fstack-protector-strong)
        add_compile_options(-fstack-protector)

        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.0)
            add_compile_options(-fcoroutines)
            add_compile_options(-Wno-error=unused-value)
        endif()

        add_compile_options(-fPIC)
        add_compile_options(-Wno-error=nonnull)
        add_compile_options(-foptimize-sibling-calls)
        add_compile_options(-Wno-stringop-overflow)
        add_compile_options(-Wno-restrict)
        add_compile_options(-Wno-error=format-truncation)

        # gcc bug, refer to https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105595
        add_compile_options(-Wno-error=subobject-linkage)

        if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 11.0)
            add_compile_options(-Wno-stringop-overread)
            add_compile_options(-Wno-maybe-uninitialized)
            add_compile_options(-Wno-array-bounds)
            add_compile_options(-Wno-aggressive-loop-optimizations)
        endif()

        if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14.0)
            # set(CMAKE_CXX_STANDARD 23)
            add_compile_options(-Wno-error=uninitialized)
        endif()
        # add_compile_options(-fconcepts-diagnostics-depth=10)
    elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.0)
            set(CMAKE_CXX_FLAGS_DEBUG "-O -g")
        endif()

        add_compile_options(-fstack-protector)
        add_compile_options(-Winconsistent-missing-override)
        add_compile_options(-foptimize-sibling-calls)
        add_compile_options(-Wno-error=unused-private-field)

        # Some Linux-specific Clang settings.  We don't want these for OS X.
        if("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
            # Tell Boost that we're using Clang's libc++.   Not sure exactly why we need to do.
            add_definitions(-DBOOST_ASIO_HAS_CLANG_LIBCXX)
            # Fix for Boost UUID on old kernel version Linux.  See https://github.com/boostorg/uuid/issues/91
            add_definitions(-DBOOST_UUID_RANDOM_PROVIDER_FORCE_POSIX)
            # Use fancy colors in the compiler diagnostics
            add_compile_options(-fcolor-diagnostics)
        endif()
    endif()

    if(SANITIZE_ADDRESS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb -fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined -fno-sanitize=alignment -fsanitize-address-use-after-scope -fsanitize-recover=all")
    endif()

    if(SANITIZE_THREAD)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb -fno-omit-frame-pointer -fsanitize=thread")
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
    add_compile_definitions(NOMINMAX)
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

if(ALLOCATOR STREQUAL "tcmalloc")
    # include(FindPkgConfig)
    # pkg_check_modules(tcmalloc REQUIRED libtcmalloc)
    # link_libraries(${tcmalloc_LINK_LIBRARIES})
elseif(ALLOCATOR STREQUAL "jemalloc")
    find_library(JEMalloc_LIB jemalloc ${VCPKG_INSTALLED_DIR} REQUIRED)
    link_libraries(${JEMalloc_LIB})
elseif(ALLOCATOR STREQUAL "mimalloc")
    find_package(mimalloc REQUIRED)
    link_libraries(mimalloc)
endif()

# rust static library linking requirements for macos
if(APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Security -framework CoreFoundation")
else()
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-as-needed -ldl")
endif()

message("CMAKE_EXE_LINKER_FLAGS: ${CMAKE_EXE_LINKER_FLAGS}")
message("CMAKE_SHARED_LINKER_FLAGS: ${CMAKE_SHARED_LINKER_FLAGS}")
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY ON)
