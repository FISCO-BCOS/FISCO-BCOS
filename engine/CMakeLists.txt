#------------------------------------------------------------------------------
# Top-level CMake file for bcos-engine
# ------------------------------------------------------------------------------


project(bcos-engine VERSION ${VERSION})

file(GLOB_RECURSE SRC_LIST "bcos-engine/*.cpp")
# EngineServiceImpl.h is the class libinitializer/EngineServiceInitializer.h instantiates in
# production (engine-driven modes); it is header-only today and its helpers live in
# EngineServiceCommon.cpp. It ships in the installed header set (see below).

add_library(engine ${SRC_LIST})
target_include_directories(engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # The installed EngineMPTStateRoot.h includes <bcos-transaction-scheduler/...>, and that
    # header in turn includes <bcos-storage/KeyPrefixes.h>. Both packages ship those headers
    # under the same include root as every other module (include/<module>/...), so no extra
    # INSTALL_INTERFACE dir is needed — and none is exported as a CMake target, which keeps
    # RocksDB out of fiscobcosTargets.
    $<INSTALL_INTERFACE:include/bcos-engine>)
target_include_directories(engine PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/transaction-scheduler>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/bcos-storage>)
# PUBLIC rlp-protocol: EthEngineService.h includes EthEngineService.inl, and the
# installed OpEngineService.inl uses <bcos-rlp-protocol/EthBlockHeader.h>. PRIVATE
# would drop include dirs for consumers that instantiate the templates.
target_link_libraries(engine PUBLIC bcos-framework bcos-task bcos-utilities ledger codec
    rlp-protocol)
set_target_properties(engine PROPERTIES UNITY_BUILD "ON")

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS engine EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
# Installed production headers, including EngineServiceImpl.h — the class
# libinitializer/EngineServiceInitializer.h builds today. Its EngineMPTStateRoot.h dependency
# is installed alongside it (and that header's transaction-scheduler / bcos-storage includes
# are installed by their own packages), so an external consumer can construct the engine
# service the product actually runs.
install(DIRECTORY "bcos-engine" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING
    PATTERN "EthEngineService.h"
    PATTERN "EthEngineService.inl"
    PATTERN "OpEngineService.h"
    PATTERN "OpEngineService.inl"
    PATTERN "EngineServiceImpl.h"
    PATTERN "EngineMPTStateRoot.h"
    PATTERN "EngineStorageCommit.h"
    PATTERN "EngineServiceCommon.h"
    PATTERN "EngineTracker.h"
    PATTERN "PayloadCache.h"
    PATTERN "PayloadId.h")
