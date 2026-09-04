#------------------------------------------------------------------------------
# Top-level CMake file for bcos-engine
# ------------------------------------------------------------------------------


project(bcos-engine VERSION ${VERSION})

file(GLOB_RECURSE SRC_LIST "bcos-engine/*.cpp")
# Helpers live in EngineServiceCommon.cpp. EngineServiceImpl is header-only;
# production still constructs it from the header.

add_library(engine ${SRC_LIST})
target_include_directories(engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-engine>)
# PRIVATE + static library: consumers still link rlp-protocol transitively
# ($<LINK_ONLY:...>); only the include dirs stay unpropagated.
target_link_libraries(engine PUBLIC bcos-framework bcos-task bcos-utilities ledger codec
    PRIVATE rlp-protocol)
set_target_properties(engine PROPERTIES UNITY_BUILD "ON")

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS engine EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-engine" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING
    PATTERN "*.h"
    PATTERN "OpEngineService.inl")