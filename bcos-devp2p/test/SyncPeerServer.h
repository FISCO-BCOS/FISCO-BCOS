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
 * @file SyncPeerServer.h
 * @brief Test-only fake Ethereum peer: holds an in-memory chain and answers
 *        eth/68 GetBlockHeaders / GetBlockBodies / Ping requests.
 * @date 2026/8/18
 */
#pragma once

#include <bcos-codec/rlp/Result.h>
#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-devp2p/rlpx/Messages.h>
#include <bcos-devp2p/sync/Block.h>
#include <bcos-devp2p/sync/HeaderValidator.h>
#include <bcos-devp2p/sync/OpHeaderValidator.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/EthWithdrawal.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <map>

namespace bcos::devp2p::test
{
using bcos::codec::rlp::unwrapOrThrow;
// Build a small in-memory Ethereum-like chain. Every block carries a real
// keccak header hash; the last block is Shanghai-style (withdrawals present).
inline std::vector<sync::Block> makeTestChain(size_t _count)
{
    std::vector<sync::Block> chain;
    bcos::h256 parentHash;  // zeros: the parent of block 0 (the anchor)
    for (size_t i = 0; i < _count; ++i)
    {
        sync::Block block;
        auto& header = block.header;
        header.number = static_cast<int64_t>(i);
        // NOTE: parentInfo.blockNumber is NOT part of the Ethereum header wire
        // format (only blockHash is); rlpDecode leaves it 0. Keep it 0 here so
        // header round-trips compare equal.
        header.parentInfo.blockNumber = 0;
        header.parentInfo.blockHash = parentHash;
        header.uncleHash = bcos::protocol::c_emptyOmmersHash;
        header.coinbase = bcos::Address{};
        header.stateRoot = bcos::crypto::HashType(
            std::string_view("0x1111111111111111111111111111111111111111111111111111111111111111"),
            bcos::crypto::HashType::FromHex);
        header.txsRoot = bcos::crypto::HashType(
            std::string_view("0x2222222222222222222222222222222222222222222222222222222222222222"),
            bcos::crypto::HashType::FromHex);
        header.receiptsRoot = bcos::crypto::HashType(
            std::string_view("0x3333333333333333333333333333333333333333333333333333333333333333"),
            bcos::crypto::HashType::FromHex);
        header.logsBloom = bcos::Bloom{};
        header.difficulty = 0;
        header.gasLimit = 30000000;
        header.gasUsed = 21000;
        header.timestamp = 1600000000 + static_cast<int64_t>(i);
        header.prevRandao = bcos::h256{};
        header.nonce = bcos::h64{};
        header.extraData = {};
        // EIP-1559 base fee. Block 0 is the first downloaded block (its parent
        // is the anchor, outside the chain) so it carries the initial 1 gwei;
        // every later block must match the recomputation from its parent for
        // the downloaded chain to pass PoS header validation.
        header.baseFee = i == 0 ? bcos::u256(1000000000) : sync::computeNextBaseFee(chain.back().header);

        // A transaction on even blocks (opaque EIP-2718 bytes — must be a COMPLETE
        // RLP element; legacy txs are lists, typed txs are 0xNN||payload).
        if (i % 2 == 0)
        {
            block.transactions.push_back(fromHex("c3010203"));
        }
        // The last block is Shanghai+: it has withdrawals (exercises the
        // 3-item eth/68 block-body format).
        if (i == _count - 1)
        {
            header.withdrawalsHash = bcos::crypto::HashType(
                std::string_view("0x4444444444444444444444444444444444444444444444444444444444444444"),
                bcos::crypto::HashType::FromHex);
            bcos::protocol::EthWithdrawalData withdrawal;
            withdrawal.index = i;
            withdrawal.validatorIndex = i;
            withdrawal.address = bcos::Address{};
            withdrawal.amount = 1000000000;
            bcos::bytes wdRlp;
            bcos::codec::rlp::encode(wdRlp, withdrawal);
            block.withdrawals = std::vector<bcos::bytes>{wdRlp};
        }

        bcos::bytes headerRlp;
        bcos::codec::rlp::encode(headerRlp, header);
        block.headerRlp = headerRlp;
        block.hash = bcos::crypto::keccak256Hash(
            bcos::bytesConstRef(headerRlp.data(), headerRlp.size()));

        chain.push_back(std::move(block));
        parentHash = chain.back().hash;
    }
    return chain;
}

// ─── OP-Stack shapes ─────────────────────────────────────────────────────────
// Genesis timestamp for the OP test chains: in the past, and (unless a test
// sets an explicit 0 fork time) resolved through the chain's OpForkSchedule.
constexpr int64_t kOpChainGenesisTs = 1700000000;

// 9-byte Holocene extraData: version 0x00 || denominator(4B) || elasticity(4B).
inline bcos::bytes opHoloceneExtraData(uint32_t _denominator, uint32_t _elasticity)
{
    bcos::bytes extra(bcos::engine::c_holoceneExtraDataBytes);
    extra[0] = bcos::engine::c_holoceneExtraDataVersion;
    bcos::bytesRef denomRef(extra.data() + 1, 4);
    bcos::toBigEndian(_denominator, denomRef);
    bcos::bytesRef elastRef(extra.data() + 5, 4);
    bcos::toBigEndian(_elasticity, elastRef);
    return extra;
}

// 17-byte Jovian extraData: version 0x01 || denominator(4B) || elasticity(4B)
// || minBaseFee(8B).
inline bcos::bytes opJovianExtraData(
    uint32_t _denominator, uint32_t _elasticity, uint64_t _minBaseFee)
{
    bcos::bytes extra(bcos::engine::c_jovianExtraDataBytes);
    extra[0] = bcos::engine::c_jovianExtraDataVersion;
    bcos::bytesRef denomRef(extra.data() + 1, 4);
    bcos::toBigEndian(_denominator, denomRef);
    bcos::bytesRef elastRef(extra.data() + 5, 4);
    bcos::toBigEndian(_elasticity, elastRef);
    bcos::bytesRef minRef(extra.data() + 9, 8);
    bcos::toBigEndian(_minBaseFee, minRef);
    return extra;
}

// Build an OP-Stack-shaped chain that PASSES validateOpHeader under `_config`
// (sync/OpHeaderValidator.h): post-merge constants, the SequencerFeeVault
// coinbase, the fork-gated fields stamped per resolveOpFork at each header's own
// timestamp (withdrawalsHash from Canyon, blob fields from Ecotone, requestsHash
// from Isthmus, Holocene 9B / Jovian 17B extraData), and every baseFee recomputed
// from its parent through calcOpBaseFeeFromFields — the same entry point the
// validator uses, so a config the validator accepts always produces a servable
// chain. Tests break rules by tampering a built header and re-encoding it (the
// server serves raw RLP), never by hand-writing invalid fields.
inline std::vector<sync::Block> makeOpTestChain(size_t _count, sync::OpChainConfig const& _config)
{
    std::vector<sync::Block> chain;
    bcos::h256 parentHash;  // zeros: the parent of block 0 (the anchor)
    for (size_t i = 0; i < _count; ++i)
    {
        sync::Block block;
        auto& header = block.header;
        header.number = static_cast<int64_t>(i);
        header.parentInfo.blockNumber = 0;  // not on the wire — see makeTestChain
        header.parentInfo.blockHash = parentHash;
        header.uncleHash = bcos::protocol::c_emptyOmmersHash;
        header.coinbase = sync::c_opSequencerFeeVault;
        header.stateRoot = bcos::crypto::HashType(
            std::string_view("0x1111111111111111111111111111111111111111111111111111111111111111"),
            bcos::crypto::HashType::FromHex);
        header.txsRoot = bcos::crypto::HashType(
            std::string_view("0x2222222222222222222222222222222222222222222222222222222222222222"),
            bcos::crypto::HashType::FromHex);
        header.receiptsRoot = bcos::crypto::HashType(
            std::string_view("0x3333333333333333333333333333333333333333333333333333333333333333"),
            bcos::crypto::HashType::FromHex);
        header.logsBloom = bcos::Bloom{};
        header.difficulty = 0;
        header.nonce = bcos::h64{};
        header.gasLimit = 30000000;
        header.gasUsed = 4000000;
        header.timestamp =
            kOpChainGenesisTs + static_cast<int64_t>(i * _config.blockTimeSeconds);
        header.prevRandao = bcos::h256{};

        // Fork-gated fields, keyed on THIS header's timestamp exactly like the
        // validator's checks.
        auto const fork = bcos::ledger::resolveOpFork(
            _config.forkSchedule, static_cast<uint64_t>(header.timestamp));
        if (fork >= bcos::ledger::OpFork::Canyon)
        {
            // Pre-Isthmus this MUST be the empty-withdrawals hash; Isthmus+ leaves
            // it unchecked at header level, so the empty hash stays valid there too.
            header.withdrawalsHash = sync::c_opEmptyWithdrawalsHash;
            // An empty (but present) withdrawals list keeps the eth/68 body in the
            // 3-item Shanghai+ shape matching the header.
            block.withdrawals = std::vector<bcos::bytes>{};
        }
        if (fork >= bcos::ledger::OpFork::Ecotone)
        {
            header.blobGasUsed = bcos::u256(0);
            header.excessBlobGas = bcos::u256(0);
            header.parentBeaconRoot = bcos::h256{};
        }
        if (fork >= bcos::ledger::OpFork::Isthmus)
        {
            header.requestsHash = sync::c_opEmptyRequestsHash;
        }
        if (fork >= bcos::ledger::OpFork::Jovian)
        {
            header.extraData = opJovianExtraData(250, 6, 0);
        }
        else if (fork >= bcos::ledger::OpFork::Holocene)
        {
            header.extraData = opHoloceneExtraData(250, 6);
        }

        // baseFee: block 0 is the anchor (never validated), every later block must
        // match the OP EIP-1559 recomputation from its parent.
        if (i == 0)
        {
            header.baseFee = bcos::u256(1000000000);
        }
        else
        {
            auto const& parent = chain.back().header;
            auto const parentFork = bcos::ledger::resolveOpFork(
                _config.forkSchedule, static_cast<uint64_t>(parent.timestamp));
            uint64_t const denominator = fork >= bcos::ledger::OpFork::Canyon ?
                                             _config.eip1559DenominatorCanyon :
                                             _config.eip1559DenominatorBedrock;
            std::span<const bcos::byte> parentExtra{
                parent.extraData.data(), parent.extraData.size()};
            header.baseFee = bcos::engine::calcOpBaseFeeFromFields(parent.gasLimit,
                parent.gasUsed, *parent.baseFee, parent.blobGasUsed, parentExtra,
                parentFork >= bcos::ledger::OpFork::Holocene,
                parentFork >= bcos::ledger::OpFork::Jovian, denominator,
                _config.eip1559Elasticity);
        }

        bcos::bytes headerRlp;
        bcos::codec::rlp::encode(headerRlp, header);
        block.headerRlp = headerRlp;
        block.hash = bcos::crypto::keccak256Hash(
            bcos::bytesConstRef(headerRlp.data(), headerRlp.size()));

        chain.push_back(std::move(block));
        parentHash = chain.back().hash;
    }
    return chain;
}

// Re-encode a tampered header in place (the server serves the raw RLP, so a
// field change only takes effect on the wire after this) and refresh the hash.
inline void reencodeOpHeader(sync::Block& _block)
{
    bcos::bytes rlp;
    bcos::codec::rlp::encode(rlp, _block.header);
    _block.headerRlp = rlp;
    _block.hash = bcos::crypto::keccak256Hash(bcos::bytesConstRef(rlp.data(), rlp.size()));
}

// Serve eth/68 requests over an established session until the peer disconnects
// or an unexpected message arrives. Runs on the server thread.
inline void serveRequests(rlpx::Session& _session, std::vector<sync::Block> const& _chain)
{
    std::map<bcos::h256, size_t> byHash;
    for (size_t i = 0; i < _chain.size(); ++i)
    {
        byHash[_chain[i].hash] = i;
    }

    for (;;)
    {
        auto msg = unwrapOrThrow(
            _session.recvMessage(), "SyncPeerServer: failed to decode a frame: ");
        if (msg.id == rlpx::baseMsg::Ping)
        {
            _session.sendMessage(rlpx::Message{rlpx::baseMsg::Pong, rlpx::encodePong()});
            continue;
        }
        if (msg.id == eth::frameId(eth::msg::GetBlockHeaders))
        {
            auto request = unwrapOrThrow(
                eth::decodeGetBlockHeaders(ref(msg.data)), "SyncPeerServer: malformed GetBlockHeaders: ");
            eth::BlockHeadersMessage response;
            response.requestId = request.requestId;
            if (request.originHash.has_value())
            {
                auto it = byHash.find(*request.originHash);
                if (it != byHash.end())
                {
                    response.headers.push_back(_chain[it->second].headerRlp);
                }
            }
            else
            {
                for (uint64_t k = 0; k < request.amount; ++k)
                {
                    int64_t idx = request.reverse ?
                                      static_cast<int64_t>(request.originNumber) -
                                          static_cast<int64_t>(k * (request.skip + 1)) :
                                      static_cast<int64_t>(request.originNumber) +
                                          static_cast<int64_t>(k * (request.skip + 1));
                    if (idx < 0 || static_cast<size_t>(idx) >= _chain.size())
                    {
                        break;
                    }
                    response.headers.push_back(_chain[static_cast<size_t>(idx)].headerRlp);
                }
            }
            _session.sendMessage(rlpx::Message{
                static_cast<uint8_t>(eth::frameId(eth::msg::BlockHeaders)),
                eth::encodeBlockHeaders(response)});
            continue;
        }
        if (msg.id == eth::frameId(eth::msg::GetBlockBodies))
        {
            auto request = unwrapOrThrow(
                eth::decodeGetBlockBodies(ref(msg.data)), "SyncPeerServer: malformed GetBlockBodies: ");
            eth::BlockBodiesMessage response;
            response.requestId = request.requestId;
            for (auto const& hash : request.hashes)
            {
                eth::BlockBody body;
                auto it = byHash.find(hash);
                if (it != byHash.end())
                {
                    auto const& block = _chain[it->second];
                    body.transactions = block.transactions;
                    body.uncles = block.uncles;
                    body.withdrawals = block.withdrawals;
                }
                response.bodies.push_back(std::move(body));
            }
            _session.sendMessage(rlpx::Message{
                static_cast<uint8_t>(eth::frameId(eth::msg::BlockBodies)),
                eth::encodeBlockBodies(response)});
            continue;
        }
        // Unknown message or disconnect: stop serving.
        break;
    }
}
}  // namespace bcos::devp2p::test
