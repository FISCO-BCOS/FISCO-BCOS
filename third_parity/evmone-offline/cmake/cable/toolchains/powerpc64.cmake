# Cable: CMake Bootstrap Library.
# Copyright 2018 Pawel Bylica.
# Licensed under the Apache License, Version 2.0. See the LICENSE file.

set(CMAKE_SYSTEM_PROCESSOR powerpc64)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_C_COMPILER powerpc64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER powerpc64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH /usr/powerpc64-linux-gnu)
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

if(${CMAKE_VERSION} VERSION_LESS 3.10.0)
    # Until CMake 3.10 the FindThreads uses try_run() check of -pthread flag,
    # what causes CMake error in crosscompiling mode. Avoid the try_run() check
    # by specifying the result up front.
    set(THREADS_PTHREAD_ARG TRUE)
endif()
