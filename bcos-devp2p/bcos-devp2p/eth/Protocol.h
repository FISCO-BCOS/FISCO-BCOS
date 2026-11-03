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
 * @brief eth/68..70 wire protocol: message codes and the messages needed for block
 *        sync and serving (Status, GetBlockHeaders/BlockHeaders, GetBlockBodies/
 *        BlockBodies, GetReceipts/Receipts, BlockRangeUpdate, NewBlockHashes).
 *        Ported from silkworm sentry/eth + sync/packets.
 * @date 2026/8/18
 */
#pragma once

#include "../rlpx/Crypto.h"
#include "../rlpx/Messages.h"
#include "ForkId.h"
#include <bcos-codec/rlp/Result.h>
#include <bcos-rlp-protocol/EthReceipt.h>
#include <bcos-utilities/FixedBytes.h>
#include <algorithm>
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
constexpr uint8_t BlockRangeUpdate = 0x11;  // eth/69+ (EIP-7642)
}  // namespace msg

// Supported eth versions. 70 (EIP-7975 partial receipt lists) and 69 (EIP-7642 block
// range + bloom-less receipts) are advertised everywhere; 68 stays negotiable for
// OUTBOUND dials only (kMinProtocolVersion): the [debug] follow_devp2p lane dials an
// op-geth whose version is not pinned anywhere in this repo, and an op-geth older than
// the geth v1.17.1 baseline that dropped eth/68 would otherwise lose its only peer.
// The inbound listener advertises from kMinServingProtocolVersion (ADR 0003: new peers
// speak 69/70; geth >= v1.17.1 refuses 68).
constexpr uint8_t kProtocolVersion = 70;  // highest eth version we support
constexpr uint8_t kMinProtocolVersion = 68;
constexpr uint8_t kMinServingProtocolVersion = 69;
// devp2p base-protocol message codes occupy 0..15; capability messages start at 16.
constexpr uint16_t kBaseProtocolLength = 16;

// On-wire frame id for an eth message (same offset for eth/68, 69 and 70).
inline uint16_t frameId(uint8_t _messageId)
{
    return static_cast<uint16_t>(kBaseProtocolLength) + _messageId;
}

// eth Status message. Two wire formats exist:
//
//   eth/68:
//   [protocolVersion, networkId, totalDifficulty, headHash, genesisHash, [forkHash, forkNext]]
//
//   eth/69+ (EIP-7642 block range):
//   [protocolVersion, networkId, genesisHash, [forkHash, forkNext], earliestBlock, latestBlock,
//    latestBlockHash]
struct StatusMessage
{
    uint64_t protocolVersion{kProtocolVersion};
    uint64_t networkId{1};
    bcos::bytes totalDifficulty;  // eth/68 only: minimal big-endian u256
    bcos::h256 headHash;          // eth/68 only
    bcos::h256 genesisHash;
    ForkId forkId;
    // eth/69+ (EIP-7642) block range fields.
    uint64_t earliestBlock{0};
    uint64_t latestBlock{0};
    bcos::h256 latestBlockHash;
    // true = encode/decode the EIP-7642 7-field form (eth/69+), false = eth/68 6-field form.
    bool eip7642{false};
};

bcos::bytes encodeStatus(StatusMessage const& _msg);
// _negotiatedVersion selects the wire layout (68 = 6-field, >= 69 = 7-field) and,
// when non-zero, the decoded protocolVersion must match it — otherwise the peer
// controls the layout selector. 0 = auto-detect from the embedded version
// (test/convenience use only).
// Malformed input is reported as an RlpError value, never thrown.
bcos::codec::rlp::RlpResult<StatusMessage> decodeStatus(
    bytesConstRef _data, uint8_t _negotiatedVersion = 0);

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
bcos::codec::rlp::RlpResult<GetBlockHeadersMessage> decodeGetBlockHeaders(bytesConstRef _data);

// BlockHeaders (eth/66+): [requestId, [headerRlp, ...]]
struct BlockHeadersMessage
{
    uint64_t requestId{0};
    std::vector<bcos::bytes> headers;  // opaque RLP-encoded headers
};

bcos::bytes encodeBlockHeaders(BlockHeadersMessage const& _msg);
bcos::codec::rlp::RlpResult<BlockHeadersMessage> decodeBlockHeaders(bytesConstRef _data);

// GetBlockBodies (eth/66+): [requestId, [blockHash, ...]]
struct GetBlockBodiesMessage
{
    uint64_t requestId{0};
    std::vector<bcos::h256> hashes;
};

bcos::bytes encodeGetBlockBodies(GetBlockBodiesMessage const& _msg);
bcos::codec::rlp::RlpResult<GetBlockBodiesMessage> decodeGetBlockBodies(bytesConstRef _data);

// BlockBodies (eth/66+): [requestId, [[txs, uncles, withdrawals?], ...]]
// Each body: [transactions, uncles, withdrawals? (Shanghai+)]. In `transactions`,
// a legacy tx is a bare RLP list element while a typed (EIP-2718) tx is an RLP
// byte string whose content is 0xNN || rlp(payload); `transactions` here always
// holds the UNWRAPPED form (0xNN || rlp(payload) for typed txs) and
// encodeBlockBodies applies the string wrapping.
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
bcos::codec::rlp::RlpResult<BlockBodiesMessage> decodeBlockBodies(bytesConstRef _data);

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
bcos::codec::rlp::RlpResult<NewBlockHashesMessage> decodeNewBlockHashes(bytesConstRef _data);

// ---------------------------------------------------------------------------
// Transaction gossip (eth/68): message-count DoS caps, enforced by the decoders.
// geth's own soft limits are far lower (it announces ~256 hashes and serves
// ~256 pooled txs per request); these are wire-shape ceilings, not policy.
// ---------------------------------------------------------------------------
inline constexpr size_t kMaxTxsPerMessage = 1024;         // Transactions / PooledTransactions
inline constexpr size_t kMaxAnnouncedHashes = 4096;       // NewPooledTransactionHashes
inline constexpr size_t kMaxGetPooledTransactions = 256;  // serving cap for GetPooledTransactions

// Transactions (0x02): RLP list of EIP-2718 envelopes. Same element rules as
// the BlockBodies transactions list (a legacy tx is a bare RLP list element, a
// typed tx an RLP string whose content is 0xNN || rlp(payload)); `transactions`
// holds the UNWRAPPED form (0xNN || rlp(payload) for typed txs — for a blob tx
// that is the whole EIP-4844 network wrapper, sidecar included) and
// encodeTransactions applies the string wrapping.
struct TransactionsMessage
{
    std::vector<bcos::bytes> transactions;
};

bcos::bytes encodeTransactions(TransactionsMessage const& _msg);
bcos::codec::rlp::RlpResult<TransactionsMessage> decodeTransactions(bytesConstRef _data);

// NewPooledTransactionHashes (0x08), eth/68 layout:
//   [types: bytes (one EIP-2718 type byte per hash), sizes: [uint, ...], hashes: [h256, ...]]
// The three fields must name the same count; decode rejects a mismatch. (eth/68
// is our negotiated floor, so the pre-68 bare-hashes form is never decoded.)
struct NewPooledTransactionHashesMessage
{
    bcos::bytes types;
    std::vector<uint64_t> sizes;
    std::vector<bcos::h256> hashes;
};

bcos::bytes encodeNewPooledTransactionHashes(NewPooledTransactionHashesMessage const& _msg);
bcos::codec::rlp::RlpResult<NewPooledTransactionHashesMessage> decodeNewPooledTransactionHashes(
    bytesConstRef _data);

// GetPooledTransactions (0x09), eth/66+ request-id form: [requestId, [hash, ...]]
// (the pre-66 form without the id is not spoken: our floor is eth/68, same wire
// convention as GetBlockHeaders above).
struct GetPooledTransactionsMessage
{
    uint64_t requestId{0};
    std::vector<bcos::h256> hashes;
};

bcos::bytes encodeGetPooledTransactions(GetPooledTransactionsMessage const& _msg);
bcos::codec::rlp::RlpResult<GetPooledTransactionsMessage> decodeGetPooledTransactions(
    bytesConstRef _data);

// PooledTransactions (0x0a): [requestId, [tx, ...]] — the tx elements follow the
// Transactions rules (typed txs are string-wrapped envelopes; a blob tx element
// is the full network wrapper). The reply may carry FEWER txs than requested
// (the serving side simply omits the ones it no longer holds).
struct PooledTransactionsMessage
{
    uint64_t requestId{0};
    std::vector<bcos::bytes> transactions;
};

bcos::bytes encodePooledTransactions(PooledTransactionsMessage const& _msg);
bcos::codec::rlp::RlpResult<PooledTransactionsMessage> decodePooledTransactions(
    bytesConstRef _data);

// ---------------------------------------------------------------------------
// GetReceipts / Receipts (0x0f / 0x10). Three wire generations:
//
//   eth/68: GetReceipts [requestId, [hash, ...]]
//           Receipts    [requestId, [[receipt, ...], ...]]
//           receipt = the receipts-trie encoding (EthReceipt.h): a legacy receipt is a
//           bare RLP list, a typed one an RLP STRING of 0xNN || rlp([status, cumGas,
//           bloom, logs]) — the same string-wrapping rule as typed transactions.
//   eth/69: same message shapes (EIP-7642), but every receipt is the 4-item list
//           [txType, status-or-postState, cumulativeGas, logs] — no bloom, no type prefix.
//   eth/70: GetReceipts [requestId, firstBlockReceiptIndex, [hash, ...]]
//           Receipts    [requestId, lastBlockIncomplete, [[receipt, ...], ...]]
//           (EIP-7975 partial lists), receipts encoded as in eth/69.
//
// `receipts` carries each receipt as the OPAQUE wire element of the negotiated version
// (mirrors BlockHeadersMessage); encodeReceiptElement/decodeReceiptElement convert
// to/from EthReceiptData per version.
// ---------------------------------------------------------------------------
struct GetReceiptsMessage
{
    uint64_t requestId{0};
    uint64_t firstBlockReceiptIndex{0};  // eth/70 only; must be 0 on 68/69
    std::vector<bcos::h256> hashes;
};

bcos::bytes encodeGetReceipts(GetReceiptsMessage const& _msg, uint8_t _version);
bcos::codec::rlp::RlpResult<GetReceiptsMessage> decodeGetReceipts(
    bytesConstRef _data, uint8_t _version);

struct ReceiptsMessage
{
    uint64_t requestId{0};
    bool lastBlockIncomplete{false};                 // eth/70 only; must be false on 68/69
    std::vector<std::vector<bcos::bytes>> receipts;  // per block: opaque receipt elements
};

bcos::bytes encodeReceipts(ReceiptsMessage const& _msg, uint8_t _version);
bcos::codec::rlp::RlpResult<ReceiptsMessage> decodeReceipts(bytesConstRef _data, uint8_t _version);

// One receipt as the wire element of `_version` (see the table above). The eth/68 form
// is EthReceipt::rlpEncode plus the typed-string wrapping; eth/69+ is the bloom-less
// 4-item list. Throws codec::rlp::RlpEncodeException on an invalid type byte (>= 0x80).
bcos::bytes encodeReceiptElement(bcos::protocol::EthReceiptData const& _receipt, uint8_t _version);
// Inverse of encodeReceiptElement over ONE complete element. On eth/69+ the bloom is
// not on the wire and is left zero; postState is set when the second item is 32 bytes.
bcos::codec::rlp::RlpResult<bcos::protocol::EthReceiptData> decodeReceiptElement(
    bytesConstRef _element, uint8_t _version);

// BlockRangeUpdate (0x11, eth/69+): [earliest, latest, latestHash]. Announces the range of
// blocks whose bodies and receipts the sender serves (headers are always served from
// genesis). Spec: a peer sending earliest > latest must be disconnected.
struct BlockRangeUpdateMessage
{
    uint64_t earliestBlock{0};
    uint64_t latestBlock{0};
    bcos::h256 latestBlockHash;

    bool valid() const { return earliestBlock <= latestBlock; }
};

bcos::bytes encodeBlockRangeUpdate(BlockRangeUpdateMessage const& _msg);
bcos::codec::rlp::RlpResult<BlockRangeUpdateMessage> decodeBlockRangeUpdate(bytesConstRef _data);

// Capability entries advertised in the Hello message, highest version first: every eth
// version in [_minVersion, kProtocolVersion]. The handshake negotiates the highest common
// version; eth/69+ peers use the EIP-7642 Status format while eth/68 peers keep the
// legacy TD/head Status. Outbound dials pass the default (68 kept, see above); the
// inbound listener passes kMinServingProtocolVersion.
inline std::vector<bcos::devp2p::rlpx::Capability> ethCapabilities(
    uint8_t _minVersion = kMinProtocolVersion, uint8_t _maxVersion = kProtocolVersion)
{
    std::vector<bcos::devp2p::rlpx::Capability> caps;
    for (int version = std::min<int>(_maxVersion, kProtocolVersion);
        version >= static_cast<int>(_minVersion); --version)
    {
        caps.push_back({std::string("eth"), static_cast<uint8_t>(version)});
    }
    return caps;
}
}  // namespace bcos::devp2p::eth
