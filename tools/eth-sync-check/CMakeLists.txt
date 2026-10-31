#------------------------------------------------------------------------------
# eth-sync-check — validate real Sepolia archive-node blocks with the devp2p
# sync core (header hash, PoS header fields, withdrawals trie root).
# ------------------------------------------------------------------------------
# Copyright (C) 2026 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# ------------------------------------------------------------------------------
find_package(CURL REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

add_executable(eth-sync-check main.cpp)
target_link_libraries(eth-sync-check PRIVATE
    bcos-devp2p
    rlp-protocol
    ledger
    protocol-tars
    tool
    bcos-crypto
    codec
    bcos-utilities
    CURL::libcurl
    jsoncpp_static)
