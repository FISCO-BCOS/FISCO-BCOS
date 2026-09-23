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
 * @file DebugEndpoint.cpp
 * @brief Geth debug namespace consumed by the fault-proof preimage server (kona-host).
 */

#include "DebugEndpoint.h"

#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/LegacyStorageMethods.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
// geth hashdb scheme: a state trie node is content-addressed by its 32-byte node hash, and
// contract code is stored under the 33-byte "c" + codeHash form that kona-host's L2Code hint
// sends (CODE_PREFIX = 'c' in kona-bin/host/src/single/handler.rs).
constexpr std::size_t c_stateNodeKeySize = 32;
constexpr std::size_t c_codeKeySize = 33;
constexpr bcos::byte c_codeKeyPrefix = 'c';
}  // namespace

task::Task<void> DebugEndpoint::dbGet(const Json::Value& request, Json::Value& response)
{
    // params: key(DATA, 32-byte node hash or 33-byte "c"+codeHash)
    // result: raw preimage(DATA); an unknown key is an error (geth DbGet parity — kona-host's
    // L2Code hint relies on the error to fall back to the unprefixed hash)
    if (request.size() < 1 || !request[0U].isString())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "debug_dbGet expects one hex key"));
    }
    auto const keyView = toView(request[0U]);
    bcos::bytes rawKey;
    try
    {
        rawKey = bcos::fromHex(keyView);
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid hex key"));
    }

    bcos::bytes preimage;
    if (rawKey.size() == c_codeKeySize && rawKey.front() == c_codeKeyPrefix)
    {
        // Contract code: strip the "c" prefix, the remaining 32 bytes are the code hash.
        // Code rows are content-addressed and immutable, so the latest state plane is correct
        // (the same read eth_getCode's historical path performs).
        std::string const codeHashStr(rawKey.begin() + 1, rawKey.end());
        auto const ledger = m_nodeService->ledger();
        auto const stateStorage = ledger->getStateStorage();
        if (!stateStorage)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InternalError, "State storage not available on this node"));
        }
        auto const codeEntry = co_await bcos::storage2::readOne(*stateStorage,
            executor_v1::StateKeyView{bcos::ledger::SYS_CODE_BINARY, codeHashStr});
        if (!codeEntry.has_value())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, "not found"));
        }
        preimage.assign(codeEntry.value().get().begin(), codeEntry.value().get().end());
    }
    else if (rawKey.size() == c_stateNodeKeySize)
    {
        // State trie node: keyed by its 32-byte node hash in the committed MPT node rows.
        auto const mptReader = m_nodeService->mptNodeReader();
        if (!mptReader)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InternalError, "MPT not enabled on this node"));
        }
        bcos::h256 const nodeHash(rawKey);
        auto const node = co_await bcos::storage2::readOne(*mptReader, nodeHash);
        if (!node.has_value())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, "not found"));
        }
        preimage = *node;
    }
    else
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "debug_dbGet key must be 32 or 33 bytes"));
    }

    Json::Value result = toHexStringWithPrefix(preimage);
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> DebugEndpoint::getRawHeader(const Json::Value& request, Json::Value& response)
{
    // params: blockHash(DATA, 32 bytes)
    // result: raw RLP header(DATA), whose keccak256 equals the block hash (geth GetRawHeader
    // parity — kona-host's preimage key is the block hash itself)
    if (request.size() < 1 || !request[0U].isString())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "debug_getRawHeader expects one block hash"));
    }
    auto const blockTag = toView(request[0U]);

    bcos::crypto::HashType hash;
    try
    {
        hash = bcos::crypto::HashType(blockTag, bcos::crypto::HashType::FromHex);
    }
    catch (std::exception const&)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid block hash"));
    }

    auto const ledger = m_nodeService->ledger();
    protocol::BlockNumber blockNumber = 0;
    try
    {
        blockNumber = co_await ledger::getBlockNumber(*ledger, hash);
    }
    catch (bcos::Error const& e)
    {
        // Same call shape as eth_getProof: an unknown hash answers GetStorageError with no
        // chained cause (a client's "Block not found"); a storage fault carries one and must
        // propagate as the internal error instead.
        if (e.errorCode() == bcos::ledger::LedgerError::GetStorageError &&
            boost::get_error_info<bcos::Error::STDError>(e) == nullptr)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
        }
        throw;
    }

    auto const block = co_await ledger::getBlockData(*ledger, blockNumber, bcos::ledger::HEADER);
    if (!block || !block->blockHeader()) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }

    // Encode the header via the pure-RLP bridge: the ms->s conversion and field projection
    // happen here, producing the exact bytes whose keccak256 is the block hash. Throws
    // RlpEncodeException for a sub-second timestamp or an incomplete fork field set.
    bcos::bytes encoded;
    try
    {
        bcos::protocol::EthBlockHeader ethHeader(*block->blockHeader());
        ethHeader.rlpEncode(encoded);
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InternalError, std::string("Header RLP encode failed: ") + e.what()));
    }

    Json::Value result = toHexStringWithPrefix(encoded);
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> DebugEndpoint::executePayload(const Json::Value&, Json::Value&)
{
    // debug_executePayload is kona-host's high-level "execution witness" hint: execute a
    // payload (parent block hash + payload attributes) and return every state node, contract
    // code and account/storage key the execution touched. It is an OP-specific extension —
    // op-reth implements it (crates/optimism/rpc/src/witness.rs, returning the
    // ExecutionWitness state/codes/keys fields), op-geth does NOT — so answering
    // MethodNotFound matches op-geth and lets kona-host fall back to the fine-grained
    // debug_dbGet / debug_getRawHeader path. Producing the witness needs the execution
    // layer to record its reads (reth's State::with_bundle_update), which the FISCO
    // executor does not track today.
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "debug_executePayload is not implemented"));
    co_return;
}
