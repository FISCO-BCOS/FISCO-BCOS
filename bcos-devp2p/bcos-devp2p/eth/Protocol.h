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
 * @file Protocol.h
 * @brief eth/68 wire protocol: message codes and the messages needed for block
 *        sync (Status, GetBlockHeaders/BlockHeaders, GetBlockBodies/BlockBodies,
 *        NewBlockHashes). Ported from silkworm sentry/eth + sync/packets.
 * @date 2026/8/18
 */
#pragma once

#include "../rlpx/Crypto.h"
#include "../rlpx/Messages.h"
#include "ForkId.h"
#include <bcos-utilities/FixedBytes.h>
#include <optional>
#include <string>
#include <vector>

namespace bcos::devp2p::eth
{
// eth/68 message codes (relative; the on-wire frame id adds kBaseProtocolLength).
namespace msg
{
constexpr uint8_t Status = 0x00;
constexpr uint8_t NewBlockHashes = 0x01;
constexpr uint8_t Transactions = 0x02;
constexpr uint8_t GetBlockHeaders = 0x03;
constexpr uint8_t BlockHeaders = 0x04;
constexpr uint8_t GetBlockBodies = 0x05;
constexpr uint8_t BlockBodies = 0x06;
constexpr uint8_t NewBlock = 0x07;
constexpr uint8_t NewPooledTransactionHashes = 0x08;
constexpr uint8_t GetPooledTransactions = 0x09;
constexpr uint8_t PooledTransactions = 0x0a;
constexpr uint8_t GetReceipts = 0x0f;
constexpr uint8_t Receipts = 0x10;
}  // namespace msg

constexpr uint8_t kProtocolVersion = 69;  // highest eth version we support
constexpr uint8_t kMinProtocolVersion = 68;
// devp2p base-protocol message codes occupy 0..15; capability messages start at 16.
constexpr uint16_t kBaseProtocolLength = 16;

// On-wire frame id for an eth message (same offset for eth/68 and eth/69).
inline uint16_t frameId(uint8_t _messageId)
{
    return static_cast<uint16_t>(kBaseProtocolLength) + _messageId;
}

// eth Status message. Two wire formats exist:
//   eth/68: [protocolVersion, networkId, totalDifficulty, headHash, genesisHash, [forkHash, forkNext]]
//   eth/69+ (EIP-8085 block range): [protocolVersion, networkId, genesisHash,
//           [forkHash, forkNext], earliestBlock, latestBlock, latestBlockHash]
struct StatusMessage
{
    uint64_t protocolVersion{kProtocolVersion};
    uint64_t networkId{1};
    bcos::bytes totalDifficulty;  // eth/68 only: minimal big-endian u256
    bcos::h256 headHash;          // eth/68 only
    bcos::h256 genesisHash;
    ForkId forkId;
    // eth/69+ (EIP-8085) block range fields.
    uint64_t earliestBlock{0};
    uint64_t latestBlock{0};
    bcos::h256 latestBlockHash;
    // true = encode/decode the EIP-8085 7-field form (eth/69+), false = eth/68 6-field form.
    bool eip8085{false};
};

bcos::bytes encodeStatus(StatusMessage const& _msg);
StatusMessage decodeStatus(bytesConstRef _data);

// GetBlockHeaders (eth/66+): [requestId, [origin, amount, skip, reverse]]
struct GetBlockHeadersMessage
{
    uint64_t requestId{0};
    std::optional<bcos::h256> originHash;  // exactly one of originHash / originNumber is set
    uint64_t originNumber{0};
    uint64_t amount{1};
    uint64_t skip{0};
    bool reverse{false};
};

bcos::bytes encodeGetBlockHeaders(GetBlockHeadersMessage const& _msg);
GetBlockHeadersMessage decodeGetBlockHeaders(bytesConstRef _data);

// BlockHeaders (eth/66+): [requestId, [headerRlp, ...]]
struct BlockHeadersMessage
{
    uint64_t requestId{0};
    std::vector<bcos::bytes> headers;  // opaque RLP-encoded headers
};

bcos::bytes encodeBlockHeaders(BlockHeadersMessage const& _msg);
BlockHeadersMessage decodeBlockHeaders(bytesConstRef _data);

// GetBlockBodies (eth/66+): [requestId, [blockHash, ...]]
struct GetBlockBodiesMessage
{
    uint64_t requestId{0};
    std::vector<bcos::h256> hashes;
};

bcos::bytes encodeGetBlockBodies(GetBlockBodiesMessage const& _msg);
GetBlockBodiesMessage decodeGetBlockBodies(bytesConstRef _data);

// BlockBodies (eth/66+): [requestId, [[txs, uncles, withdrawals?], ...]]
// Each body: [transactions (list of raw EIP-2718 tx bytes), uncles (list of raw headers),
//             withdrawals? (list of raw EIP-4895 withdrawal RLP, Shanghai+)]
struct BlockBody
{
    std::vector<bcos::bytes> transactions;
    std::vector<bcos::bytes> uncles;
    // nullopt = pre-Shanghai body; an (possibly empty) list for Shanghai+.
    std::optional<std::vector<bcos::bytes>> withdrawals;
};

struct BlockBodiesMessage
{
    uint64_t requestId{0};
    std::vector<BlockBody> bodies;
};

bcos::bytes encodeBlockBodies(BlockBodiesMessage const& _msg);
BlockBodiesMessage decodeBlockBodies(bytesConstRef _data);

// NewBlockHashes: [[blockHash, number], ...]
struct NewBlockHashesMessage
{
    struct Entry
    {
        bcos::h256 hash;
        uint64_t number{0};
    };
    std::vector<Entry> entries;
};

bcos::bytes encodeNewBlockHashes(NewBlockHashesMessage const& _msg);
NewBlockHashesMessage decodeNewBlockHashes(bytesConstRef _data);

// Capability entries advertised in the Hello message: eth/69 and eth/68. The
// handshake negotiates the highest common version; eth/69 peers use the EIP-8085
// Status format while eth/68 peers keep the legacy TD/head Status.
inline std::vector<bcos::devp2p::rlpx::Capability> ethCapabilities()
{
    return {{std::string("eth"), kProtocolVersion},
        {std::string("eth"), kMinProtocolVersion}};
}
}  // namespace bcos::devp2p::eth
