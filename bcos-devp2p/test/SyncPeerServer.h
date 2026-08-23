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

#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-devp2p/rlpx/Messages.h>
#include <bcos-devp2p/sync/Block.h>
#include <bcos-devp2p/sync/HeaderValidator.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/EthWithdrawal.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <map>

namespace bcos::devp2p::test
{
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
        header.uncleHash = sync::emptyOmmersHash();
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
        auto msg = _session.recvMessage();
        if (msg.id == rlpx::baseMsg::Ping)
        {
            _session.sendMessage(rlpx::Message{rlpx::baseMsg::Pong, rlpx::encodePong()});
            continue;
        }
        if (msg.id == eth::frameId(eth::msg::GetBlockHeaders))
        {
            auto request = eth::decodeGetBlockHeaders(ref(msg.data));
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
            auto request = eth::decodeGetBlockBodies(ref(msg.data));
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
