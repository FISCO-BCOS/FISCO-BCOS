function(create_build_info)
    # Set build platform; to be written to BuildInfo.h
    set(FISCO_BCOS_BUILD_OS "${CMAKE_SYSTEM_NAME}")

    if (CMAKE_COMPILER_IS_MINGW)
        set(FISCO_BCOS_BUILD_COMPILER "mingw")
    elseif (CMAKE_COMPILER_IS_MSYS)
        set(FISCO_BCOS_BUILD_COMPILER "msys")
    elseif (CMAKE_COMPILER_IS_GNUCXX)
        set(FISCO_BCOS_BUILD_COMPILER "g++")
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(FISCO_BCOS_BUILD_COMPILER "clang")
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
        set(FISCO_BCOS_BUILD_COMPILER "appleclang")
    else ()
        set(FISCO_BCOS_BUILD_COMPILER "unknown")
    endif ()

    set(FISCO_BCOS_BUILD_PLATFORM "${FISCO_BCOS_BUILD_OS}/${FISCO_BCOS_BUILD_COMPILER}")


    if (CMAKE_BUILD_TYPE)
        set(_cmake_build_type ${CMAKE_BUILD_TYPE})
    else()
        set(_cmake_build_type "${CMAKE_CFG_INTDIR}")
    endif()
    # Generate header file containing useful build information
    add_custom_target(BuildInfo.h ALL
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -DFISCO_BCOS_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        -DFISCO_BCOS_BUILDINFO_IN="${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/BuildInfo.h.in"
        -DFISCO_BCOS_DST_DIR="${PROJECT_BINARY_DIR}/include"
        -DFISCO_BCOS_CMAKE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cmake"
        -DFISCO_BCOS_BUILD_TYPE="${_cmake_build_type}"
        -DFISCO_BCOS_BUILD_OS="${FISCO_BCOS_BUILD_OS}"
        -DFISCO_BCOS_BUILD_COMPILER="${FISCO_BCOS_BUILD_COMPILER}"
        -DFISCO_BCOS_BUILD_PLATFORM="${FISCO_BCOS_BUILD_PLATFORM}"
        -DFISCO_BCOS_BUILD_NUMBER="${FISCO_BCOS_BUILD_NUMBER}"
        -DFISCO_BCOS_VERSION_SUFFIX="${VERSION_SUFFIX}"
        -DPROJECT_VERSION="${PROJECT_VERSION}"
        -P "${FISCO_BCOS_SCRIPTS_DIR}/buildinfo.cmake"
        )
    include_directories(BEFORE ${PROJECT_BINARY_DIR})
endfunction()
