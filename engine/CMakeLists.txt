#------------------------------------------------------------------------------
# Top-level CMake file for bcos-engine
# ------------------------------------------------------------------------------


project(bcos-engine VERSION ${VERSION})

file(GLOB_RECURSE SRC_LIST "bcos-engine/*.cpp")
# EngineServiceImpl.h is the legacy template, instantiated by tests only (EthLegacyEngine /
# parity); production builds EthEngineService / OpEngineService. It is header-only today and
# its helpers live in EngineServiceCommon.cpp. It ships in the installed header set (see below).

add_library(engine ${SRC_LIST})
target_include_directories(engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # Installed EthEngineService.h / EngineMPTStateRoot.h include
    # <bcos-transaction-scheduler/...> and <bcos-storage/...>. Those headers land at
    # ${prefix}/include/<module>/..., so an installed consumer of the exported `engine`
    # target needs the package include root. This is an include path only —
    # transaction-scheduler is still not exported, so RocksDB stays out of
    # fiscobcosTargets. Keep include/bcos-engine so quoted intra-module includes
    # (`"EngineServiceCommon.h"`) still resolve after install.
    $<INSTALL_INTERFACE:include>
    $<INSTALL_INTERFACE:include/bcos-engine>)
target_include_directories(engine PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/transaction-scheduler>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/bcos-storage>)
# PUBLIC rlp-protocol: EthEngineService.h includes EthEngineService.inl, and the
# installed OpEngineService.inl uses both <bcos-rlp-protocol/EthBlockHeader.h> (the
# pre-Canyon computeHash identity) and <bcos-rlp-protocol/BlockHeaderHash.h>. PRIVATE
# would drop include dirs for consumers that instantiate the templates.
# PRIVATE opstack-executor: OpEngineService.cpp's Jovian blobGasUsed equality gate uses the
# header-inline bcos::evm::opstack::daFootprintOfEnvelopes (OpBlockExecute.h), so this is an
# INCLUDE-PATH dependency only — the archive no longer holds a reference to any
# opstack-executor symbol (the inlined fastlz/estimated chain in RollupCost.h is header-inline
# too, so bcos-evm-opstack is not pulled in either). Wrapped in $<BUILD_INTERFACE:> so it stays
# out of the installed engine interface, which needs no new dependency; an exported static
# library may not name a non-exported link target. In-tree consumers get the include path.
# No cycle: opstack-executor's closure never links engine.
target_link_libraries(engine PUBLIC bcos-framework bcos-task bcos-utilities ledger codec
    rlp-protocol)
target_link_libraries(engine PRIVATE $<BUILD_INTERFACE:opstack-executor>)
set_target_properties(engine PROPERTIES UNITY_BUILD "ON")

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS engine EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
# Installed headers, including EngineServiceImpl.h — the legacy template, instantiated by
# tests only (EthLegacyEngine / parity); production builds EthEngineService / OpEngineService.
# Its EngineMPTStateRoot.h dependency is installed alongside it (and that header's
# transaction-scheduler / bcos-storage includes are installed by their own packages), so an
# external consumer can instantiate the legacy engine service in tests too.
# Deliberately NOT installed: OpEngineService.h / OpEngineService.inl. The pair pulls
# bcos-evm/opstack-executor headers (RollupCost.h, OpCommitments.h) whose packages install
# no headers of their own, so the installed copies could not compile out of tree — they are
# in-tree consumables (libinitializer). The Eth pair stays: its dependencies are all
# installed packages (framework / bcos-ledger / bcos-crypto / rlp-protocol).
install(DIRECTORY "bcos-engine" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING
    PATTERN "EthEngineService.h"
    PATTERN "EthEngineService.inl"
    PATTERN "EngineServiceImpl.h"
    PATTERN "EngineMPTStateRoot.h"
    PATTERN "EngineStorageCommit.h"
    PATTERN "EngineServiceCommon.h"
    PATTERN "EngineTracker.h"
    PATTERN "PayloadCache.h"
    PATTERN "PayloadId.h")
