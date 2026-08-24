#------------------------------------------------------------------------------
# Top-level CMake file for bcos-single-consensus
#------------------------------------------------------------------------------
project(bcos-single-consensus VERSION ${VERSION})

file(GLOB_RECURSE SRC_LIST "bcos-single-consensus/*.cpp")

add_library(single-consensus ${SRC_LIST} ${HEADERS})
target_include_directories(single-consensus PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-single-consensus>)
target_link_libraries(single-consensus PUBLIC bcos-framework bcos-task bcos-utilities bcos-crypto ledger)
set_target_properties(single-consensus PROPERTIES UNITY_BUILD "ON")

include(GNUInstallDirs)
install(TARGETS single-consensus EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-single-consensus" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
