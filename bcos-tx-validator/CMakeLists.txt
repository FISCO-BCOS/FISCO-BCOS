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

# The nonce checkers moved here from bcos-txpool: they are admission state, and the pool is not
# the only ingress that needs them. Deliberately NOT linking any transaction pool -- the
# dependency runs the other way, and a cycle would make the split pointless.
target_link_libraries(bcos-tx-validator PUBLIC
    bcos-framework bcos-protocol ledger TBB::tbb)
set_target_properties(bcos-tx-validator PROPERTIES UNITY_BUILD "ON")

include(GNUInstallDirs)
install(TARGETS bcos-tx-validator EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-tx-validator" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h")
