#------------------------------------------------------------------------------
# Top-level CMake file for bcos-tx-validator
# ------------------------------------------------------------------------------
# Copyright (C) 2026 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# ------------------------------------------------------------------------------

file(GLOB_RECURSE SRCS bcos-tx-validator/*.cpp)

find_package(TBB CONFIG REQUIRED)

add_library(bcos-tx-validator ${SRCS})
target_include_directories(bcos-tx-validator PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-tx-validator>)

# rlp-protocol decodes the signed envelope; protocol-tars is where the mirror lives and is the
# only way to write it -- protocol::Transaction exposes no setter for a business field, by
# design (it is an immutable-after-signing value object). `ledger` supplies getSystemConfig and
# LedgerMethods; TBB backs the nonce checkers' BucketMap.
#
# Deliberately NOT linking either transaction pool: the nonce checkers live here now and the
# dependency runs one way, from the pools to this module. A cycle would make the split
# pointless. No intx either: the 512-bit balance arithmetic uses bcos::u512 from
# bcos-utilities, which bcos-framework already propagates.
target_link_libraries(bcos-tx-validator PUBLIC
    bcos-framework bcos-protocol bcos-crypto ledger rlp-protocol protocol-tars TBB::tbb)
set_target_properties(bcos-tx-validator PROPERTIES UNITY_BUILD "ON")

if(TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-tx-validator EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-tx-validator" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h")
