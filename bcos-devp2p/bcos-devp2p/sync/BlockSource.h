/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file BlockSource.h
 * @brief The read interface the eth serving loop (PeerServer) answers peers from:
 *        canonical-chain lookups by number plus a hash -> number index. Production
 *        wraps the ledger (libinitializer/LedgerBlockSource.h); tests wrap the
 *        in-memory chain of test/SyncPeerServer.h.
 * @date 2026/11/3
 */
#pragma once

#include "../eth/Protocol.h"
#include <bcos-rlp-protocol/EthReceipt.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstdint>
#include <optional>
#include <vector>

namespace bcos::devp2p::sync
{
// The EIP-7642 served range: bodies and receipts of [earliest, latest] are available
// (headers always from genesis); latestHash identifies the head.
struct BlockRange
{
    uint64_t earliest{0};
    uint64_t latest{0};
    bcos::h256 latestHash;

    bool operator==(BlockRange const&) const = default;
};

class BlockSourceInterface
{
public:
    virtual ~BlockSourceInterface() = default;

    // Current head/range; read per request so a serving loop sees head advances.
    virtual BlockRange range() = 0;
    // Canonical block number of a known block hash; nullopt for an unknown hash.
    virtual std::optional<uint64_t> numberByHash(bcos::h256 const& _hash) = 0;
    // Wire RLP of the canonical header at _number; nullopt past the head.
    virtual std::optional<bcos::bytes> headerRlp(uint64_t _number) = 0;
    // The canonical body (unwrapped transactions, uncles, withdrawals when Shanghai+).
    virtual std::optional<eth::BlockBody> body(uint64_t _number) = 0;
    // The canonical block's receipts in transaction order (empty for an empty block).
    virtual std::optional<std::vector<bcos::protocol::EthReceiptData>> receipts(
        uint64_t _number) = 0;
};
}  // namespace bcos::devp2p::sync
