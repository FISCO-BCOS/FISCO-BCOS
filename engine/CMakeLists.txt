#------------------------------------------------------------------------------
# Top-level CMake file for bcos-engine
# ------------------------------------------------------------------------------


project(bcos-engine VERSION ${VERSION})

file(GLOB_RECURSE SRC_LIST "bcos-engine/*.cpp")
# EngineServiceImpl.h is leftover until parity fixtures migrate off it.
# The .cpp TU is gone; production helpers live in EngineServiceCommon.cpp.

add_library(engine ${SRC_LIST})
target_include_directories(engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-engine>)
target_link_libraries(engine PUBLIC bcos-framework bcos-task bcos-utilities ledger codec rlp-protocol)
set_target_properties(engine PROPERTIES UNITY_BUILD "ON")

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS engine EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
# Production headers only (finding CD). EngineServiceImpl.h is leftover / test-only.
install(DIRECTORY "bcos-engine" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING
    PATTERN "EthEngineService.h"
    PATTERN "EthEngineService.inl"
    PATTERN "OpEngineService.h"
    PATTERN "OpEngineService.inl"
    PATTERN "EngineServiceCommon.h"
    PATTERN "EngineTracker.h"
    PATTERN "PayloadCache.h"
    PATTERN "PayloadId.h")
