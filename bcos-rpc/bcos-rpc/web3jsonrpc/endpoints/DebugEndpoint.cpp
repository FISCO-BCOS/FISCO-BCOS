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

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/LegacyStorageMethods.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/RpcChainPolicy.h>
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
constexpr bcos::byte c_codeKeyPrefix = 'c';
}  // namespace

task::Task<void> DebugEndpoint::dbGet(const Json::Value& request, Json::Value& response)
{
    // params: key (DATA); result: the stored preimage (DATA)
    // kona-host (bin/host/src/single/handler.rs) sends two key shapes: geth hashdb's code key
    // 'c' || codeHash (33 bytes) and the bare 32-byte keccak hash (trie nodes). Both resolve
    // against the two keccak-addressed stores: MPT nodes (mptNodeReader, "/mpt/" rows) and
    // bytecode (s_code_binary). Content addressing makes either store a valid answer for either
    // shape; the shape only picks which store to try first. A miss answers -32000 "not found"
    // (geth's server-error code); kona-host then retries its code hint with the bare-hash form.
    co_await requireOpStackLane(*m_nodeService->ledger(), "debug_dbGet");
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
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid key: not hex"));
    }

    auto const isCodeKey = rawKey.size() == h256::SIZE + 1 && rawKey.front() == c_codeKeyPrefix;
    if (!isCodeKey && rawKey.size() != h256::SIZE)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Web3DefaultError, "not found"));
    }
    auto const mptReader = m_nodeService->mptNodeReader();
    if (!mptReader) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, "MPT not enabled on this node"));
    }
    h256 const hash(rawKey.data() + (isCodeKey ? 1 : 0), h256::FromPointer);
    auto const stateStorage = m_nodeService->ledger()->getStateStorage();

    std::optional<bcos::bytes> value;
    if (isCodeKey)
    {
        if (stateStorage)
        {
            value = co_await readCodeByHash(*stateStorage, hash);
        }
    }
    if (!value)
    {
        value = co_await bcos::storage2::readOne(*mptReader, hash);
    }
    if (!value && !isCodeKey && stateStorage)
    {
        value = co_await readCodeByHash(*stateStorage, hash);
    }
    if (!value)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Web3DefaultError, "not found"));
    }

    Json::Value result = toHexStringWithPrefix(*value);
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> DebugEndpoint::getRawHeader(const Json::Value& request, Json::Value& response)
{
    // params: blockNumberOrHash (QTY|TAG|DATA 32B)
    // result: the RLP-encoded Ethereum header (DATA); keccak256(result) is the block hash
    // eth_getBlockBy* reports (canonicalBlockHash), which is what kona-host checks.
    co_await requireOpStackLane(*m_nodeService->ledger(), "debug_getRawHeader");
    if (request.size() < 1 || !request[0U].isString())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "debug_getRawHeader expects one block parameter"));
    }
    auto const blockTag = toView(request[0U]);
    auto const ledger = m_nodeService->ledger();

    protocol::BlockNumber blockNumber = 0;
    protocol::BlockNumber head = 0;
    if (blockTag.size() == 66 && blockTag[0] == '0' && (blockTag[1] == 'x' || blockTag[1] == 'X'))
    {
        bcos::crypto::HashType hash;
        try
        {
            hash = bcos::crypto::HashType(blockTag, bcos::crypto::HashType::FromHex);
        }
        catch (std::exception const&)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid block hash"));
        }
        try
        {
            blockNumber = co_await ledger::getBlockNumber(*ledger, hash);
            head = co_await ledger::getCurrentBlockNumber(*ledger);
        }
        catch (bcos::Error const& e)
        {
            if (e.errorCode() == bcos::ledger::LedgerError::GetStorageError &&
                boost::get_error_info<bcos::Error::STDError>(e) == nullptr)
            {
                BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
            }
            throw;
        }
    }
    else
    {
        // QTY|TAG: resolve through the shared tag resolver (same as eth_getBlockByNumber).
        auto const latest = co_await ledger::getCurrentBlockNumber(*ledger);
        auto [number, _] = bcos::rpc::getBlockNumberByTag(latest, blockTag,
            m_nodeService->safeBlockDepth(), m_nodeService->finalizedBlockDepth(), std::nullopt,
            std::nullopt, false);
        blockNumber = number;
        head = latest;
    }
    if (blockNumber < 0 || blockNumber > head)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    auto const block = co_await ledger::getBlockData(*ledger, blockNumber, ledger::HEADER);
    auto const headerPtr = block ? block->blockHeader() : nullptr;
    if (!headerPtr) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    auto const& header = *headerPtr;
    // The published hash (canonicalBlockHash) is keccak256 of this RLP for an OP header and for
    // an Ethereum-versioned one, whose stored hash is that RLP hash (calculateRLPHash) — the
    // rollup genesis from [eth_genesis_header] is the latter. A native FISCO header's hash is
    // not, so serving it would hand the host bytes that do not hash to the published hash.
    if (!protocol::isOpEthereumBlock(header) &&
        header.ethBlockVersion() == protocol::EthBlockVersion::NON_ETH) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InternalError, "Block " + std::to_string(blockNumber) + " has no Ethereum header"));
    }
    Json::Value result =
        toHexStringWithPrefix(protocol::EthBlockHeader::encodeHeader(header));
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
