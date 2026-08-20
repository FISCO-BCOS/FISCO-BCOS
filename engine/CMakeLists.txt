#------------------------------------------------------------------------------
# Top-level CMake file for bcos-engine
# ------------------------------------------------------------------------------


project(bcos-engine VERSION ${VERSION})

file(GLOB_RECURSE SRC_LIST "bcos-engine/*.cpp")

add_library(engine ${SRC_LIST})
target_include_directories(engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-engine>)
target_link_libraries(engine PUBLIC bcos-framework bcos-task bcos-utilities ledger rlp-protocol)
# rpc is a build-tree-only private link (opEnvelopeToTars in EngineServiceImpl.cpp uses
# Web3Transaction). rpc is NOT in the fiscobcosTargets export set, so a bare `PRIVATE rpc` would
# make install(EXPORT fiscobcosTargets) fail ("requires target rpc that is not in any export set").
# $<BUILD_INTERFACE:...> keeps the link at build time and drops it from the installed export --
# correct because Web3Transaction.h is only used by the .cpp (D6: never in EngineServiceImpl.h).
target_link_libraries(engine PRIVATE $<BUILD_INTERFACE:rpc>)
set_target_properties(engine PROPERTIES UNITY_BUILD "ON")

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS engine EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-engine" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")