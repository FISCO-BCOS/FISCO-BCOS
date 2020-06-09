#------------------------------------------------------------------------------
# Setting compiling for FISCO-BCOS.
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

# common settings
set(ETH_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ETH_SCRIPTS_DIR ${ETH_CMAKE_DIR}/scripts)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

if (("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU") OR ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
        set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CCACHE_PROGRAM}")
    endif()
    # set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "/usr/bin/time")
    # set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "/usr/bin/time")
    # Use ISO C++11 standard language.
    set(CMAKE_CXX_FLAGS "-std=c++11 -pthread -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -fexceptions")
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    # Enables all the warnings about constructions that some users consider questionable,
    # and that are easy to avoid.  Also enable some extra warning flags that are not
    # enabled by -Wall.   Finally, treat at warnings-as-errors, which forces developers
    # to fix warnings as they arise, so they don't accumulate "to be fixed later".
    add_compile_options(-Werror)
    add_compile_options(-Wall)
    add_compile_options(-pedantic)
    add_compile_options(-Wextra)
    # add_compile_options(-Wno-unused-variable)
    # add_compile_options(-Wno-unused-parameter)
    # add_compile_options(-Wno-unused-function)
    # add_compile_options(-Wno-missing-field-initializers)
    # Disable warnings about unknown pragmas (which is enabled by -Wall).
    add_compile_options(-Wno-unknown-pragmas)
    add_compile_options(-fno-omit-frame-pointer)
    # for boost json spirit
    add_compile_options(-DBOOST_SPIRIT_THREADSAFE)
    # for tbb, TODO: https://software.intel.com/sites/default/files/managed/b2/d2/TBBRevamp.pdf
    add_compile_options(-DTBB_SUPPRESS_DEPRECATED_MESSAGES=1)
    # build deps lib Release
    set(_only_release_configuration "-DCMAKE_BUILD_TYPE=Release")

    if(BUILD_STATIC)
        SET(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
        SET(BUILD_SHARED_LIBRARIES OFF)
        SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
        # SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Bdynamic -ldl -lpthread -Wl,-Bstatic -static-libstdc++ ")
    endif ()

    if(TESTS)
        add_compile_options(-DBOOST_TEST_THREAD_SAFE)
    endif ()

    if(PROF)
    	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
		SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
		SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -pg")
    endif ()

    # Configuration-specific compiler settings.
    set(CMAKE_CXX_FLAGS_DEBUG          "-Og -g  -DFISCO_DEBUG")
    set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

    option(USE_LD_GOLD "Use GNU gold linker" ON)
    if (USE_LD_GOLD)
        execute_process(COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
        if ("${LD_VERSION}" MATCHES "GNU gold")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
            set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=gold")
        endif ()
    endif ()

    # Additional GCC-specific compiler settings.
    if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")

        add_compile_options(-Wa,-march=generic64)
        # Check that we've got GCC 4.7 or newer.
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)
        if (NOT (GCC_VERSION VERSION_GREATER 4.7 OR GCC_VERSION VERSION_EQUAL 4.7))
            message(FATAL_ERROR "${PROJECT_NAME} requires g++ 4.7 or greater.")
        endif ()
        if (GCC_VERSION VERSION_LESS 5.0)
            add_compile_options(-Wno-unused-variable)
            add_compile_options(-Wno-missing-field-initializers)
        endif ()
		set(CMAKE_C_FLAGS "-std=c99")

		# Strong stack protection was only added in GCC 4.9.
		# Use it if we have the option to do so.
		# See https://lwn.net/Articles/584225/
        if (GCC_VERSION VERSION_GREATER 4.9 OR GCC_VERSION VERSION_EQUAL 4.9)
            add_compile_options(-fstack-protector-strong)
            add_compile_options(-fstack-protector)
        endif()
    # Additional Clang-specific compiler settings.
    elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.0)
            set(CMAKE_CXX_FLAGS_DEBUG          "-O -g -DFISCO_DEBUG")
        endif()
        # set(CMAKE_CXX_FLAGS "-stdlib=libc++ ${CMAKE_CXX_FLAGS}")
        add_compile_options(-fstack-protector)
        add_compile_options(-Winconsistent-missing-override)
        # Some Linux-specific Clang settings.  We don't want these for OS X.
        if ("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
            # Tell Boost that we're using Clang's libc++.   Not sure exactly why we need to do.
            add_definitions(-DBOOST_ASIO_HAS_CLANG_LIBCXX)
            # Use fancy colors in the compiler diagnostics
            add_compile_options(-fcolor-diagnostics)
        endif()
    endif()

    if (COVERAGE)
        set(TESTS ON)
        if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
            set(CMAKE_CXX_FLAGS "-g --coverage ${CMAKE_CXX_FLAGS}")
            set(CMAKE_C_FLAGS "-g --coverage ${CMAKE_C_FLAGS}")
            set(CMAKE_SHARED_LINKER_FLAGS "--coverage ${CMAKE_SHARED_LINKER_FLAGS}")
            set(CMAKE_EXE_LINKER_FLAGS "--coverage ${CMAKE_EXE_LINKER_FLAGS}")
        elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
            add_compile_options(-Wno-unused-command-line-argument)
            set(CMAKE_CXX_FLAGS "-g -fprofile-arcs -ftest-coverage ${CMAKE_CXX_FLAGS}")
            set(CMAKE_C_FLAGS "-g -fprofile-arcs -ftest-coverage ${CMAKE_C_FLAGS}")
        endif()
        find_program(LCOV_TOOL lcov)
        message(STATUS "lcov tool: ${LCOV_TOOL}")
        if (LCOV_TOOL)
            add_custom_target(coverage
                COMMAND ${LCOV_TOOL} -o ${CMAKE_BINARY_DIR}/coverage.info.in -c -d ${CMAKE_BINARY_DIR}/
                COMMAND ${LCOV_TOOL} -r ${CMAKE_BINARY_DIR}/coverage.info.in '/usr*' '${CMAKE_SOURCE_DIR}/deps**' '${CMAKE_SOURCE_DIR}/evmc*' ‘${CMAKE_SOURCE_DIR}/fisco-bcos*’  -o ${CMAKE_BINARY_DIR}/coverage.info
                COMMAND genhtml -q -o ${CMAKE_BINARY_DIR}/CodeCoverage ${CMAKE_BINARY_DIR}/coverage.info)
        else ()
            message(FATAL_ERROR "Can't find lcov tool. Please install lcov")
        endif()
    endif ()
else ()
    message(WARNING "Your compiler is not tested, if you run into any issues, we'd welcome any patches.")
endif ()

if (SANITIZE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=${SANITIZE}")
    if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
        set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fsanitize-blacklist=${CMAKE_SOURCE_DIR}/sanitizer-blacklist.txt")
    endif()
endif()

# rust static library linking requirements for macos
if(APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Security")
endif()

