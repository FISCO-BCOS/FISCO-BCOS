cmake_minimum_required(VERSION 3.10)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.3" CACHE STRING "Minimum OS X deployment version")

include(Version)
project(bcos-tars-protocol VERSION ${VERSION})

# for tars generator
set(TARS_HEADER_DIR ${CMAKE_BINARY_DIR}/generated/bcos-tars-protocol/tars)
find_program(TARS_TARS2CPP tars2cpp REQUIRED)

file(GLOB_RECURSE TARS_INPUT "*.tars")

# generate tars
if(TARS_INPUT)
    foreach(TARS_FILE ${TARS_INPUT})
        get_filename_component(TARS_NAME ${TARS_FILE} NAME_WE)
        get_filename_component(TARS_PATH ${TARS_FILE} PATH)
        add_custom_command(
            OUTPUT ${TARS_HEADER_DIR}/${TARS_NAME}.h
            WORKING_DIRECTORY ${TARS_PATH}
            COMMAND ${TARS_TARS2CPP} ${TARS_FILE} --dir=${TARS_HEADER_DIR}
            COMMENT "generating ${TARS_FILE} to ${TARS_HEADER_DIR}"
            VERBATIM
        )

        list(APPEND OUT_TARS_H_LIST ${TARS_HEADER_DIR}/${TARS_NAME}.h)
    endforeach()
endif()

set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${OUT_TARS_H_LIST}")

file(GLOB_RECURSE SRC_LIST bcos-tars-protocol/*.cpp)

find_package(tarscpp REQUIRED)
find_package(TBB REQUIRED)

add_library(${TARS_PROTOCOL_TARGET} ${SRC_LIST} ${OUT_TARS_H_LIST})
target_include_directories(${TARS_PROTOCOL_TARGET} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${TARSCPP_ROOT}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include/bcos-tars-protocol>)
target_link_libraries(${TARS_PROTOCOL_TARGET} PUBLIC bcos-concepts bcos-crypto tarscpp::tarsservant tarscpp::tarsutil TBB::tbb)

if(TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS ${TARS_PROTOCOL_TARGET} EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-tars-protocol" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${CMAKE_BINARY_DIR}/generated/bcos-tars-protocol" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")

# # for doxygen
# include(BuildDocs)
# buildDoc(bcos-tars-protocol-doc)

# for code coverage
# if (COVERAGE)
# include(Coverage)
# config_coverage("tars-protocol-cov" "'/usr*' '${CMAKE_CURRENT_SOURCE_DIR}/bcos-cmake-scripts*' '${CMAKE_CURRENT_SOURCE_DIR}/test/bcos-test*'")
# endif ()
