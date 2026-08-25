/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file EthEndpoint.cpp
 * @author: kyonGuo
 * @date 2024/3/21
 */

#include "EthEndpoint.h"
#include "bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-executor/src/Common.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/storage/LegacyStorageMethods.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rlp-protocol/Web3TxEnvelope.h>  // isTypedWeb3Envelope (envelope-sourced chainId gate)
#include <bcos-rpc/Common.h>
#include <bcos-rpc/util.h>
#include <bcos-rpc/web3jsonrpc/model/BlockResponse.h>
#include <bcos-rpc/web3jsonrpc/model/CallRequest.h>
#include <bcos-rpc/web3jsonrpc/model/ReceiptResponse.h>
#include <bcos-rpc/web3jsonrpc/model/TransactionResponse.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

using namespace bcos;
using namespace bcos::rpc;

task::Task<void> EthEndpoint::protocolVersion(const Json::Value&, Json::Value&)
{
    // TODO: impl this, this returns eth p2p protocol version
    BOOST_THROW_EXCEPTION(
        JsonRpcException(MethodNotFound, "This API has not been implemented yet!"));
    co_return;
}
task::Task<void> EthEndpoint::syncing(const Json::Value&, Json::Value& response)
{
    auto const sync = m_nodeService->sync();
    auto status = sync->getSyncStatus();
    Json::Value result;
    if (!status.has_value())
    {
        result = false;
    }
    else
    {
        result = Json::objectValue;
        auto [currentBlock, highestBlock] = status.value();
        result["startingBlock"] = "0x0";
        result["currentBlock"] = toQuantity(currentBlock);
        result["highestBlock"] = toQuantity(highestBlock);
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::coinbase(const Json::Value&, Json::Value& response)
{
    auto const nodeId = m_nodeService->consensus()->consensusConfig()->nodeID();
    auto const address = right160(crypto::keccak256Hash(ref(nodeId->data())));
    Json::Value result = address.hexPrefixed();
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::chainId(const Json::Value&, Json::Value& response)
{
    // Reads via LedgerConfig (not a raw SYS_CONFIG single-key read) so that L2-mode
    // governance (SystemConfig.sol via L2ConfigLoader, wired in A4) flows through one
    // path. getLedgerConfig over-fetches (~5 storage reads) per call; per plan decision,
    // RPC-side caching is deferred to Phase B.
    auto const ledger = m_nodeService->ledger();
    auto const ledgerConfig = co_await ledger::getLedgerConfig(*ledger);
    Json::Value result;
    if (ledgerConfig->chainId().has_value())
    {
        result = toQuantity(fromEvmC(ledgerConfig->chainId().value()));
    }
    else
    {
        result = "0x0";  // 0 for default
    }
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::mining(const Json::Value&, Json::Value& response)
{
    Json::Value result = false;
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::hashrate(const Json::Value&, Json::Value& response)
{
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::gasPrice(const Json::Value&, Json::Value& response)
{
    // result: gasPrice(QTY)
    auto const ledger = m_nodeService->ledger();
    // TODO)): gas price can wrap in a class
    auto config = co_await ledger::getSystemConfig(*ledger, ledger::SYSTEM_KEY_TX_GAS_PRICE);
    Json::Value result;
    if (config.has_value())
    {
        auto [gasPrice, _] = config.value();
        auto const value = std::stoull(gasPrice, nullptr, 16);
        result = toQuantity(value);
    }
    else
    {
        result = "0x0";
    }
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::accounts(const Json::Value&, Json::Value& response)
{
    Json::Value result = Json::arrayValue;
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::blockNumber(const Json::Value&, Json::Value& response)
{
    auto ledger = m_nodeService->ledger();
    auto number = co_await ledger::getCurrentBlockNumber(*ledger);
    Json::Value result = toQuantity(number);
    buildJsonContent(result, response);
}

/// Historical state-read error code (eth_getStorageAt / getBalance / getTransactionCount /
/// getCode): a historical block whose MPT state root is not available on this node (MPT never
/// enabled, or the block predates MPT activation), or — scenario A — a dormant account absent
/// from the incomplete trie.
constexpr int32_t EthHistoricalStateUnavailable = -32004;

/// Historical MPT read context: the block's committed state root plus whether the chain's
/// storage tries are complete. getProof reads the same flag (feature_l2_ethereum_compat) to
/// distinguish "exclusion provably means zero" (scenario B, complete tries) from "cold slot
/// absent from an incomplete trie" (scenario A, mid-chain MPT activation).
struct HistoricalMptContext
{
    bcos::h256 stateRoot;
    bool fullTrie = false;
};

/// Resolve a historical block's committed MPT state root and scenario flag, applying the same
/// checks as getProof (generateProof's BlockNotCommitted): the block must exist, the node must
/// have a local MPT node reader, and the state root must be present in MPT node storage.
/// Throws a JsonRpcException on any failure — a historical query is never silently served from
/// the latest state. The empty root is a legal "no accounts" root (genesis / pre-MPT / empty
/// blocks): the empty trie has no node rows, so it is NOT a "root not committed" error — the
/// scenario flag below still governs how absence at it reads.
bcos::task::Task<HistoricalMptContext> resolveHistoricalMptContext(
    bcos::ledger::LedgerInterface& ledger, bcos::protocol::BlockNumber blockNumber,
    std::shared_ptr<rpc::NodeService::MPTNodeReader> const& mptReader)
{
    auto const block = co_await ledger::getBlockData(ledger, blockNumber, bcos::ledger::HEADER);
    if (!block || !block->blockHeader()) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    auto const stateRoot = block->blockHeader()->stateRoot();
    if (!mptReader) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, "MPT not enabled on this node"));
    }
    // An empty state root (block 0 / empty blocks / pre-MPT blocks) is a
    // legal "no accounts" root, not a missing node row — skip the root-presence check and let
    // MPTReadView (which handles emptyRootHash as "no accounts") plus the scenario flag decide
    // how absence reads: scenario B -> zero; scenario A -> honest dormant-account error.
    if (stateRoot != bcos::ledger::mpt::emptyRootHash()) [[likely]]
    {
        if (!co_await bcos::storage2::readOne(*mptReader, stateRoot)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                EthHistoricalStateUnavailable, "Block stateRoot not in MPT node storage"));
        }
    }
    // The scenario flag decides how absence at this root is read (getProof's fullTrie).
    // Single-flag read (feature_l2_ethereum_compat): one SYS_CONFIG row instead of
    // fetchAllFeatures' ~60-key scan; degrades to false (scenario A) on read failure, the
    // same honest default as getFeatures' empty-set fallback.
    auto const fullTrie = co_await ledger::getFeature(
        ledger, ledger::Features::Flag::feature_l2_ethereum_compat, blockNumber);
    co_return HistoricalMptContext{stateRoot, fullTrie};
}

task::Task<void> EthEndpoint::getBalance(const Json::Value& request, Json::Value& response)
{
    // params: address(DATA), blockNumber(QTY|TAG)
    // result: balance(QTY)
    auto address = toView(request[0U]);
    if (address.starts_with("0x") || address.starts_with("0X"))
    {
        address.remove_prefix(2);
    }
    std::string addressStr(address);
    boost::algorithm::to_lower(addressStr);
    auto const blockTag = toView(request[1U]);
    auto [blockNumber, isLatest] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << "eth_getBalance" << LOG_KV("address", address)
                        << LOG_KV("blockTag", blockTag) << LOG_KV("blockNumber", blockNumber)
                        << LOG_KV("isLatest", isLatest);
    }
    auto const ledger = m_nodeService->ledger();
    u256 balance = 0;
    if (isLatest)
    {
        if (auto const entry = co_await ledger::getStorageAt(
                *ledger, addressStr, bcos::executor::ACCOUNT_BALANCE, /*blockNumber*/ 0);
            entry.has_value())
        {
            auto const balanceStr = std::string(entry.value().get());
            balance = u256(balanceStr);
        }
        else
        {
            WEB3_LOG(TRACE) << LOG_DESC("getBalance failed, return 0 by defualt")
                            << LOG_KV("address", address);
        }
    }
    else
    {
        // Historical state: the account's balance from the block's committed MPT root.
        // Absence semantics are scenario-driven, same rule as getProof: scenario B (complete
        // tries) reads a missing account as zero; scenario A cannot distinguish a dormant
        // account from a non-existent one, so it errors explicitly.
        auto const mptReader = m_nodeService->mptNodeReader();
        auto const ctx = co_await resolveHistoricalMptContext(*ledger, blockNumber, mptReader);
        bcos::ledger::mpt::MPTReadView view{*mptReader, ctx.stateRoot};
        auto const account = co_await view.readAccount(
            bcos::Address{addressStr, bcos::Address::FromHex, bcos::Address::AlignRight});
        if (account)
        {
            balance = account->balance;
        }
        else if (!ctx.fullTrie) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                EthHistoricalStateUnavailable, "Account not in trie (dormant in scenario A)"));
        }
    }
    Json::Value result = toQuantity(std::move(balance));
    buildJsonContent(result, response);
}
/// Render a stored slot value (raw big-endian bytes in the account table) as the spec's
/// fixed-width 32-byte DATA: left-padded with zeros to a full word, exactly like geth's
/// common.BytesToHash(value).Hex(). Storage rows carry the 32-byte value in practice
/// (EVMAccount::setStorage), but this keeps the RPC output deterministic at 32 bytes even
/// if a row was written narrower. A value wider than 32 bytes is corrupt; the low 32 bytes
/// (the numeric value) are kept, never a wider-than-spec output.
static std::string storageValueToData(std::string_view value)
{
    bcos::bytes word(32, 0);
    auto const copyLen = (std::min)(value.size(), word.size());
    std::copy_n(value.data(), copyLen, word.end() - static_cast<std::ptrdiff_t>(copyLen));
    return toHex(word, "0x");
}

task::Task<void> EthEndpoint::getStorageAt(const Json::Value& request, Json::Value& response)
{
    // params: address(DATA), position(QTY), blockNumber(QTY|TAG)
    // result: value(DATA)
    auto address = toView(request[0U]);
    if (address.starts_with("0x") || address.starts_with("0X"))
    {
        address.remove_prefix(2);
    }
    std::string addressStr(address);
    boost::algorithm::to_lower(addressStr);
    auto position = toView(request[1u]);
    std::string positionStr = std::string(
        (position.starts_with("0x") || position.starts_with("0X")) ? position.substr(2) : position);
    if (position.size() % 2 != 0)
    {
        positionStr.insert(0, "0");
    }
    // A QUANTITY position is at most 256 bits (32 bytes = 64 hex digits); anything wider is
    // an invalid param per the spec. Note the std::string FixedBytes overload silently
    // truncates wider input via fromHex, so reject explicitly before decoding.
    FixedBytes<32> positionBytes;
    try
    {
        if (positionStr.size() > 64) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid storage position"));
        }
        positionBytes = FixedBytes<32>(positionStr, FixedBytes<32>::FromHex);
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid storage position"));
    }

    auto const blockTag = toView(request[2U]);
    auto [blockNumber, isLatest] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << "eth_getStorageAt" << LOG_KV("address", address)
                        << LOG_KV("pos", positionStr) << LOG_KV("blockTag", blockTag)
                        << LOG_KV("blockNumber", blockNumber) << LOG_KV("isLatest", isLatest);
    }
    auto const ledger = m_nodeService->ledger();

    // System-contract addresses (0x1000 range, etc.) are stored under the "/sys/" prefix by
    // EVMAccount; user accounts under "/apps/". Picking the right prefix here keeps
    // eth_getStorageAt consistent with both the genesis alloc import and the v2 executor
    // (which both go through EVMAccount) — same logic as Ledger::getStorageAt.
    auto const tablePrefix =
        precompiled::contains(bcos::precompiled::c_systemTxsAddress, std::string_view{addressStr}) ?
            ledger::SYS_DIRECTORY::SYS_APPS :
            ledger::SYS_DIRECTORY::USER_APPS;
    auto const contractTableName = getContractTableName(tablePrefix, addressStr);

    // The empty-slot value: a 32-byte zero, matching the flat read's padded rendering.
    constexpr const char* c_emptyStorageValue =
        "0x0000000000000000000000000000000000000000000000000000000000000000";

    if (isLatest)
    {
        // Latest state: fork a fresh view of GlobalStateStorage's COMMITTED plane and read
        // the flat KV — a consistent point-in-time snapshot of the last committed block
        // (cache -> committed backend, no in-flight pending layers). This is the same plane
        // getBalance / getTransactionCount / getCode read (committed ledger / scheduler):
        // "latest" means the last committed block, per Ethereum semantics. Operators who
        // want the pending window (in-flight executed, not yet committed layers) visible
        // can wire a provider that forks GlobalStateStorage::fork() instead — the default
        // wiring (AirNodeInitializer) is committed-only. The provider is unset on nodes
        // with no local state storage (tars-built NodeService); those fall back to the
        // ledger, which serves the same committed plane.
        Json::Value result;
        auto const& stateStorageProvider = m_nodeService->stateStorageProvider();
        if (stateStorageProvider)
        {
            auto const stateStorage = stateStorageProvider();
            if (stateStorage)
            {
                if (auto const entry = co_await bcos::storage2::readOne(*stateStorage,
                        executor_v1::StateKey{contractTableName, positionBytes.toRawString()});
                    entry.has_value())
                {
                    result = storageValueToData(entry.value().get());
                }
                else
                {
                    result = c_emptyStorageValue;
                }
                buildJsonContent(result, response);
                co_return;
            }
        }
        if (auto const entry = co_await ledger::getStorageAt(
                *ledger, addressStr, positionBytes.toRawString(), /*blockNumber*/ 0);
            entry.has_value())
        {
            result = storageValueToData(entry.value().get());
        }
        else
        {
            result = c_emptyStorageValue;
        }
        buildJsonContent(result, response);
        co_return;
    }

    // Historical state: served from the MPT at the block's committed state root (same checks
    // as getProof / getBalance / getTransactionCount / getCode).
    auto const mptReader = m_nodeService->mptNodeReader();
    auto const ctx = co_await resolveHistoricalMptContext(*ledger, blockNumber, mptReader);

    // Query the slot through the MPT at that root: account leaf -> storageRoot -> slot leaf
    // (slotKeyHash(slot)). Absence semantics are scenario-driven, exactly like getProof:
    //  - scenario B (ctx.fullTrie): the trie is complete, so absent account / empty storage
    //    trie / absent slot all read zero — Ethereum semantics at a committed root;
    //  - scenario A (mid-chain activation): a dormant account absent from the trie is
    //    indistinguishable from a non-existent one → explicit error; a slot absent from the
    //    (incomplete) storage trie — whether the account has no storage in the trie yet
    //    (storageRoot == emptyRootHash(), first touch wrote only nonce/balance/code) or the
    //    storage trie has no leaf for this slot — → fall back to the flat KV. The flat value
    //    is authoritative when the slot was never written after activation; if it was written
    //    *after* the requested block the fallback returns that later value, since
    //    ledger::getStorageAt ignores blockNumber today (the same limitation as getProof's
    //    SlotNotInMPT fallback). Still strictly better than reporting zero.
    std::optional<std::string> flatFallback;  // scenario-A dormant-slot fallback rendering
    bcos::u256 value = 0;
    {
        bcos::ledger::mpt::MPTReadView view{*mptReader, ctx.stateRoot};
        auto const account = co_await view.readAccount(
            bcos::Address{addressStr, bcos::Address::FromHex, bcos::Address::AlignRight});
        if (!account)
        {
            if (!ctx.fullTrie) [[unlikely]]
            {
                BOOST_THROW_EXCEPTION(JsonRpcException(
                    EthHistoricalStateUnavailable, "Account not in trie (dormant in scenario A)"));
            }
        }
        else
        {
            // The slot may be absent two ways: the account has no storage in the trie yet
            // (storageRoot == emptyRootHash — first touch wrote only nonce/balance/code,
            // MPTBuilder.h:307-310), or the storage trie has no leaf for this slot. Both mean
            // "unknown at this root", not "zero", on a scenario-A chain — so the flat fallback
            // below covers both.
            bool slotInTrie = false;
            if (account->storageRoot != bcos::ledger::mpt::emptyRootHash())
            {
                bcos::ledger::mpt::Trie trie{*mptReader, account->storageRoot};
                if (auto const slot =
                        co_await trie.get(bcos::ledger::mpt::slotKeyHash(positionBytes)))
                {
                    value = bcos::ledger::mpt::decodeStorageValue(bcos::ref(*slot));
                    slotInTrie = true;
                }
            }
            if (!slotInTrie && !ctx.fullTrie) [[unlikely]]
            {
                if (auto const flat = co_await ledger::getStorageAt(
                        *ledger, addressStr, positionBytes.toRawString(), blockNumber);
                    flat.has_value())
                {
                    flatFallback = storageValueToData(flat.value().get());
                }
            }
        }
    }
    // Render like the flat path: a fixed-width 32-byte hex value.
    Json::Value result;
    if (flatFallback)
    {
        result = *flatFallback;
    }
    else
    {
        result = toHex(bcos::h256{value}.ref(), "0x");
    }
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::getTransactionCount(const Json::Value& request, Json::Value& response)
{
    // params: address(DATA), blockNumber(QTY|TAG)
    // result: nonce(QTY)
    auto address = toView(request[0u]);
    if (address.starts_with("0x") || address.starts_with("0X"))
    {
        address.remove_prefix(2);
    }
    std::string addressStr(address);
    boost::algorithm::to_lower(addressStr);
    auto const blockTag = toView(request[1U]);
    auto [blockNumber, isLatest] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << "eth_getTransactionCount" << LOG_KV("address", address)
                        << LOG_KV("blockTag", blockTag) << LOG_KV("blockNumber", blockNumber)
                        << LOG_KV("isLatest", isLatest);
    }
    if (blockTag == PendingBlock)
    {
        // try to fetch in txpool first
        auto const txpool = m_nodeService->txpool();
        if (auto const nonce = co_await txpool->getWeb3PendingNonce(address))
        {
            WEB3_LOG(TRACE) << "eth_getTransactionCount pending tx from txpool"
                            << LOG_KV("address", address) << LOG_KV("blockTag", blockTag)
                            << LOG_KV("nonce", nonce.value());
            Json::Value result = toQuantity(nonce.value());
            buildJsonContent(result, response);
            co_return;
        }
    }

    auto const ledger = m_nodeService->ledger();
    u256 nonce = 0;
    if (isLatest)
    {
        if (auto const entry = co_await ledger::getStorageAt(
                *ledger, addressStr, bcos::ledger::ACCOUNT_TABLE_FIELDS::NONCE, /*blockNumber*/ 0);
            entry.has_value())
        {
            nonce = u256(entry.value().get());
        }
    }
    else
    {
        // Historical state: the account's nonce from the block's committed MPT root.
        // Scenario-driven absence semantics, same rule as getBalance / getProof: scenario B
        // reads a missing account as zero; scenario A errors for a dormant account.
        auto const mptReader = m_nodeService->mptNodeReader();
        auto const ctx = co_await resolveHistoricalMptContext(*ledger, blockNumber, mptReader);
        bcos::ledger::mpt::MPTReadView view{*mptReader, ctx.stateRoot};
        auto const account = co_await view.readAccount(
            bcos::Address{addressStr, bcos::Address::FromHex, bcos::Address::AlignRight});
        if (account)
        {
            nonce = account->nonce;
        }
        else if (!ctx.fullTrie) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                EthHistoricalStateUnavailable, "Account not in trie (dormant in scenario A)"));
        }
    }
    Json::Value result = toQuantity(nonce);
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::getBlockTxCountByHash(
    const Json::Value& request, Json::Value& response)
{
    // params: blockHash(DATA)
    // result: transactionCount(QTY)
    auto const hashStr = toView(request[0U]);
    auto hash = crypto::HashType(hashStr, crypto::HashType::FromHex);
    auto const ledger = m_nodeService->ledger();
    Json::Value result;
    try
    {
        auto number = co_await ledger::getBlockNumber(*ledger, hash);
        auto block =
            co_await ledger::getBlockData(*ledger, number, bcos::ledger::TRANSACTIONS_HASH);
        result = toQuantity(block->transactionsHashSize());
    }
    catch (...)
    {
        result = "0x0";
    }
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::getBlockTxCountByNumber(
    const Json::Value& request, Json::Value& response)
{
    // params: blockNumber(QTY|TAG)
    // result: transactionCount(QTY)
    auto const number = fromQuantity(std::string(toView(request[0U])));
    auto const ledger = m_nodeService->ledger();
    Json::Value result;
    try
    {
        auto const block =
            co_await ledger::getBlockData(*ledger, number, bcos::ledger::TRANSACTIONS_HASH);
        result = toQuantity(block->transactionsHashSize());
    }
    catch (...)
    {
        result = "0x0";
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getUncleCountByBlockHash(const Json::Value&, Json::Value& response)
{
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getUncleCountByBlockNumber(const Json::Value&, Json::Value& response)
{
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getCode(const Json::Value& request, Json::Value& response)
{
    // params: address(DATA), blockNumber(QTY|TAG)
    // result: code(DATA)
    auto address = toView(request[0U]);
    if (address.starts_with("0x") || address.starts_with("0X"))
    {
        address.remove_prefix(2);
    }
    std::string addressStr(address);
    boost::algorithm::to_lower(addressStr);
    auto const blockTag = toView(request[1u]);
    auto [blockNumber, isLatest] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << "eth_getCode" << LOG_KV("address", address)
                        << LOG_KV("blockTag", blockTag) << LOG_KV("blockNumber", blockNumber)
                        << LOG_KV("isLatest", isLatest);
    }

    bcos::bytes code;
    if (isLatest)
    {
        auto const scheduler = m_nodeService->scheduler();
        struct Awaitable
        {
            bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
            std::string& m_address;
            bcos::bytes& m_code;
            Error::Ptr m_error = nullptr;
            constexpr static bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                m_scheduler->getCode(m_address, [this, handle](auto&& error, auto&& code) {
                    if (error)
                    {
                        m_error = std::move(error);
                    }
                    else
                    {
                        m_code = std::move(code);
                    }
                    handle.resume();
                });
            }
            void await_resume()
            {
                if (m_error)
                {
                    BOOST_THROW_EXCEPTION(*m_error);
                }
            }
        };
        // Note: Awaitable must be declared as a local variable,
        // and then co_await the local variable,
        // otherwise the object managed by the Awaitable variable will become invalid.
        Awaitable awaitable{
            .m_scheduler = scheduler,
            .m_address = addressStr,
            .m_code = code,
        };
        co_await awaitable;
    }
    else
    {
        // Historical state: the account's code from the block's committed MPT root, resolved
        // through its codeHash into the content-addressed code store (s_code_binary). Reads
        // the LATEST plane (ledger->getStateStorage()) — equivalent to the historical one
        // because code rows are content-addressed and NEVER deleted: EVM code is immutable
        // and EVMAccount's write path has no deletion (lifecycle precondition; if code
        // cleanup/compaction is ever introduced, this historical read must pin the code
        // blob at the block, not read the latest plane). Scenario-driven absence semantics:
        // scenario B reads a missing account as "no code"; scenario A errors for a dormant
        // account.
        auto const ledger = m_nodeService->ledger();
        auto const mptReader = m_nodeService->mptNodeReader();
        auto const ctx = co_await resolveHistoricalMptContext(*ledger, blockNumber, mptReader);
        bcos::ledger::mpt::MPTReadView view{*mptReader, ctx.stateRoot};
        auto const account = co_await view.readAccount(
            bcos::Address{addressStr, bcos::Address::FromHex, bcos::Address::AlignRight});
        if (account)
        {
            if (account->codeHash != bcos::ledger::mpt::emptyCodeHash())
            {
                auto const stateStorage = ledger->getStateStorage();
                std::string const codeHashStr = account->codeHash.toRawString();
                if (auto const codeEntry = co_await bcos::storage2::readOne(*stateStorage,
                        executor_v1::StateKeyView{bcos::ledger::SYS_CODE_BINARY, codeHashStr});
                    codeEntry.has_value())
                {
                    code.assign(codeEntry.value().get().begin(), codeEntry.value().get().end());
                }
            }
        }
        else if (!ctx.fullTrie) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                EthHistoricalStateUnavailable, "Account not in trie (dormant in scenario A)"));
        }
    }
    Json::Value result = toHexStringWithPrefix(code);
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::sign(const Json::Value&, Json::Value& response)
{
    // params: address(DATA), message(DATA)
    // result: signature(DATA)
    Json::Value result = "0x00";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::signTransaction(const Json::Value&, Json::Value& response)
{
    // params: transaction(TX), address(DATA)
    // result: signedTransaction(DATA)
    Json::Value result = "0x00";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::sendTransaction(const Json::Value&, Json::Value& response)
{
    // params: transaction(TX)
    // result: transactionHash(DATA)
    Json::Value result = "0x0000000000000000000000000000000000000000000000000000000000000000";
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> EthEndpoint::sendRawTransaction(const Json::Value& request, Json::Value& response)
{
    // params: signedTransaction(DATA)
    // result: transactionHash(DATA)
    auto rawTx = toView(request[0U]);
    auto rawTxBytes = fromHexWithPrefix(rawTx);
    auto bytesRef = bcos::ref(rawTxBytes);
    // Authoritative first-byte dispatch (RawTransactionDispatch.h). FISCO's OP policy rejects
    // blob (type-3) at the gate — op-geth's decodeTyped accepts them, so this is a deliberate
    // acceptance divergence, not a reference check (see the type-byte gate in OpTransition.h /
    // OpCommon.h). Deposits (0x7e) can only be injected by the consensus layer through the
    // Engine API, never through the public transaction pool.
    switch (engine::dispatchRawTransaction(bytesRef))
    {
    case engine::RawTransactionKind::Blob:
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "blob transactions are not supported"));
    case engine::RawTransactionKind::Deposit:
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "deposit transactions cannot be submitted via eth_sendRawTransaction"));
    default:
        break;
    }
    Web3Transaction web3Tx;
    if (auto const error = codec::rlp::decode(bytesRef, web3Tx); error != nullptr) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, error->errorMessage()));
    }
    // Defense-in-depth: the first-byte dispatch above already rejects 0x7e, and decode cannot
    // produce type==Deposit from any other first byte. op-geth likewise rejects Deposit from
    // eth_sendRawTransaction (ErrTxTypeNotSupported); deposits only enter via the
    // derivation/engine path, never from a client RPC submission.
    if (web3Tx.type == TransactionType::Deposit) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams,
            "Deposit (0x7e) transactions are not supported via eth_sendRawTransaction"));
    }
    auto encodeTxHash = web3Tx.txHash();
    // Capture the chainId decoded from the SIGNED envelope BEFORE takeToTarsTransaction moves
    // web3Tx away (kyonRay R4 #3): re-walking the raw bytes via web3ChainIdFromEnvelope would
    // duplicate the decoder and invite drift — the walker is uint64 while the decoder is u256,
    // so a chainId above 2^64-1 would decode fine here but read as nullopt in the walker. The
    // walker stays for the P2P/tars path where only a Transaction is available (part 4b).
    auto const web3ChainId = web3Tx.chainId;

    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [m_tx = web3Tx.takeToTarsTransaction()]() mutable { return &m_tx; });

// for web3.eth.sendRawTransaction, return the hash of raw transaction
#if 0
    if (auto web3TxHash = bcos::crypto::keccak256Hash(bcos::ref(rawTxBytes));
        web3TxHash != encodeTxHash) [[unlikely]]
    {
        bytes web3Encoded;
        codec::rlp::encode(web3Encoded, web3Tx);
        WEB3_LOG(WARNING) << "sendRawTransaction hash not match"
                          << LOG_KV("inputHash", web3TxHash.hexPrefixed())
                          << LOG_KV("encodedHash", encodeTxHash.hexPrefixed())
                          << " payload: " << web3Tx.toString() << " origin: " << toHex(rawTxBytes)
                          << " encoded: " << toHex(web3Encoded);
    }
#endif
    tx->mutableInner().extraTransactionHash.assign(encodeTxHash.begin(), encodeTxHash.end());

    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << LOG_DESC("sendRawTransaction") << web3Tx.toString();
    }

    // Single-node consensus mode: route directly to the in-process mempool, bypassing
    // txpool and P2P broadcast. EngineService seals these txs into blocks on a timer.
    if (auto* memPool = m_nodeService->memPool()) [[unlikely]]
    {
        // The mempool path bypasses TxValidator, so admission here is deliberately lighter
        // than the txpool path: signature + EIP-155 chainId are checked below; web3 nonce,
        // ledger nonce/blockLimit, EIP-3860 initcode size and balance are NOT — the mode is
        // aimed at single-node EEST reproduction where fixtures use arbitrary nonces and
        // unfunded senders (MemPoolImpl::add only rejects null / tainted / duplicate-hash /
        // unparseable-nonce transactions). ChainId IS checked because dropping it would
        // disable EIP-155 replay protection: a transaction signed for another chain would
        // execute here and consume the sender's nonce.
        auto chainIdConfig = co_await ledger::getSystemConfig(
            *m_nodeService->ledger(), ledger::SYSTEM_KEY_WEB3_CHAIN_ID);
        if (!chainIdConfig)
        {
            // Fail closed when web3_chain_id is unconfigured: without a node chainId to
            // compare against, an EIP-155 tx signed for a foreign chain would execute here
            // and consume the sender's nonce (EIP-155 replay-protection bypass). op-geth
            // always has a genesis chainId, so it has no such open default. Only pre-EIP-155
            // legacy (no chainId in the envelope) is exempt — there is nothing to compare.
            auto const envelopeChainId = web3ChainId;
            if (envelopeChainId.has_value() ||
                bcos::rlp::protocol::isTypedWeb3Envelope(tx->extraTransactionBytes()))
            {
                WEB3_LOG(WARNING) << LOG_DESC("sendRawTransaction: web3_chain_id not configured")
                                  << LOG_KV("txChainId", tx->chainId());
                BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "invalid chainId"));
            }
        }
        else
        {
            auto [chainIdStr, _] = chainIdConfig.value();
            // Validate against the SIGNED envelope; base TxValidator::validateChainId reads the
            // tars mirror and is migrated to this envelope-keyed form in part 4b (#5477): typed
            // txs get no "0" exemption (op-geth modernSigner); only pre-EIP-155 legacy (no
            // chainId in the envelope) is exempt — a deliberate divergence (geth/op-geth
            // default rejects unprotected txs at the RPC layer; part 5 adds the config gate).
            // Parse as u256 via the shared helper (ledger::parseWeb3ChainId); a corrupted
            // config rejects the tx.
            auto expected = ledger::parseWeb3ChainId(chainIdStr);
            if (!expected.has_value())
            {
                BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "invalid chainId"));
            }
            auto const envelopeChainId = web3ChainId;
            if (envelopeChainId.has_value() && *envelopeChainId != *expected)
            {
                WEB3_LOG(WARNING) << LOG_DESC("sendRawTransaction chainId mismatch")
                                  << LOG_KV("txChainId", tx->chainId())
                                  << LOG_KV("nodeChainId", chainIdStr);
                BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "invalid chainId"));
            }
            // Same typed-envelope guard as TxValidator::validateChainId (mirror→envelope
            // migration lands in 4b): a typed tx whose
            // chainId field cannot be parsed (nullopt) gets no "0" exemption — a malformed
            // typed envelope is rejected here rather than deferring to signature recovery.
            if (!envelopeChainId.has_value() &&
                bcos::rlp::protocol::isTypedWeb3Envelope(tx->extraTransactionBytes()))
            {
                BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "invalid chainId"));
            }
        }
        // The mempool rejects tainted transactions (MemPoolImpl::add throws
        // InvalidTaintedTransaction), so recover the sender / verify the signature the same
        // way TxValidator does for the txpool path before adding.
        try
        {
            auto cryptoSuite = m_nodeService->blockFactory()->cryptoSuite();
            tx->verify(*cryptoSuite->hashImpl(), *cryptoSuite->signatureImpl());
        }
        catch (std::exception const& e)
        {
            WEB3_LOG(WARNING) << LOG_DESC("sendRawTransaction mempool verify failed")
                              << LOG_KV("reason", boost::diagnostic_information(e));
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "invalid transaction signature"));
        }
        std::vector<protocol::Transaction::Ptr> txs;
        txs.push_back(std::move(tx));
        memPool->add(txs);
        Json::Value result = encodeTxHash.hexPrefixed();
        buildJsonContent(result, response);
        co_return;
    }

    auto txpool = m_nodeService->txpool();
    if (!txpool) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::InternalError, "TXPool not available!"));
    }
    co_await txpool->broadcastTransaction(*tx);
    auto const txResult = co_await txpool->submitTransaction(std::move(tx), m_syncTransaction);
    if (txResult->status() == 0)
    {
        Json::Value result = encodeTxHash.hexPrefixed();
        buildJsonContent(result, response);
    }
    else
    {
        auto status = static_cast<protocol::TransactionStatus>(txResult->status());
        Json::Value errorData = Json::objectValue;
        errorData["txHash"] = encodeTxHash.hexPrefixed();
        auto output = toHex(txResult->transactionReceipt()->output(), "0x");
        auto msg = fmt::format("VM Exception while processing transaction, reason: {}, msg: {}",
            protocol::toString(status), output);
        errorData["message"] = msg;
        errorData["data"] = output;
        buildJsonErrorWithData(errorData, InternalError, std::move(msg), response);
    }
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        WEB3_LOG(TRACE) << LOG_DESC("sendRawTransaction finished")
                        << LOG_KV("status", txResult->status())
                        << LOG_KV("hash", encodeTxHash.hexPrefixed())
                        << LOG_KV("rsp", printJson(response));
    }
}

task::Task<void> EthEndpoint::call(const Json::Value& request, Json::Value& response)
{
    co_await call(request, response, nullptr, false);
}
task::Task<void> EthEndpoint::call(
    const Json::Value& request, Json::Value& response, u256* gasUsed, bool isEstimate)
{
    // params: transaction(TX), blockNumber(QTY|TAG)
    // result: data(DATA)
    auto scheduler = m_nodeService->scheduler();
    if (!scheduler)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::InternalError, "Scheduler not available!"));
    }
    auto [valid, call] = decodeCallRequest(request[0U]);
    if (!valid)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid call request!"));
    }
    auto const blockTag = toView(request[1U]);
    auto [blockNumber, isLatest] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << LOG_DESC("eth_call") << LOG_KV("call", call)
                        << LOG_KV("blockTag", blockTag) << LOG_KV("blockNumber", blockNumber);
    }
    auto tx = call.takeToTransaction(
        m_nodeService->blockFactory()->transactionFactory(), isEstimate ? scheduler : nullptr);
    struct Awaitable
    {
        bcos::scheduler::SchedulerInterface& m_scheduler;
        bcos::protocol::Transaction::Ptr& m_tx;
        // Engaged for a non-latest tag: route through callAtBlock (M13.2) so the scheduler
        // executes against that block's state; disengaged keeps the latest-state call().
        std::optional<protocol::BlockNumber> m_historicalBlock;
        Error::Ptr m_error;
        Json::Value& m_response;
        u256* m_gasUsed;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            auto callback = [this, handle](Error::Ptr&& error, auto&& result) {
                if (error)
                {
                    m_error = std::move(error);
                }
                else
                {
                    auto output = toHexStringWithPrefix(result->output());
                    if (result->status() == static_cast<int32_t>(protocol::TransactionStatus::None))
                    {
                        m_response["jsonrpc"] = "2.0";
                        m_response["result"] = output;
                    }
                    else
                    {
                        // https://docs.infura.io/api/networks/ethereum/json-rpc-methods/eth_call#returns
                        Json::Value jsonResult = Json::objectValue;
                        jsonResult["code"] = result->status();
                        jsonResult["message"] = result->message();
                        jsonResult["data"] = output;
                        m_response["jsonrpc"] = "2.0";
                        m_response["error"] = std::move(jsonResult);
                    }

                    if (m_gasUsed)
                    {
                        *m_gasUsed = result->gasUsed();
                    }
                }

                handle.resume();
            };
            if (m_historicalBlock)
            {
                m_scheduler.callAtBlock(m_tx, *m_historicalBlock, std::move(callback));
            }
            else
            {
                m_scheduler.call(m_tx, std::move(callback));
            }
        }
        void await_resume()
        {
            if (m_error)
            {
                BOOST_THROW_EXCEPTION(*m_error);
            }
        }
    } awaitable{.m_scheduler = *scheduler,
        .m_tx = tx,
        .m_historicalBlock = isLatest ? std::nullopt : std::make_optional(blockNumber),
        .m_error = {},
        .m_response = response,
        .m_gasUsed = gasUsed};
    co_await awaitable;
}
task::Task<void> EthEndpoint::estimateGas(const Json::Value& request, Json::Value& response)
{
    // params: transaction(TX), blockNumber(QTY|TAG)
    // result: gas(QTY)
    auto const& tx = request[0U];
    auto const blockTag = toView(request[1U]);
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << LOG_DESC("eth_estimateGas") << LOG_KV("tx", printJson(tx))
                        << LOG_KV("blockTag", blockTag) << LOG_KV("blockNumber", blockNumber);
    }

    u256 gasUsed;
    Json::Value callResponse;
    co_await call(request, callResponse, std::addressof(gasUsed), true);

    if (!callResponse.isMember("error"))
    {
        Json::Value result = toQuantity(gasUsed);
        buildJsonContent(result, response);
    }
    else
    {
        response = std::move(callResponse);
    }
}
task::Task<void> EthEndpoint::getBlockByHash(const Json::Value& request, Json::Value& response)
{
    // params: blockHash(DATA), fullTransaction(Boolean)
    // result: block(BLOCK)
    auto const blockHash = toView(request[0U]);
    auto const fullTransaction = request[1U].asBool();
    auto const ledger = m_nodeService->ledger();
    Json::Value result = Json::objectValue;
    try
    {
        auto const number = co_await ledger::getBlockNumber(
            *ledger, crypto::HashType(blockHash, crypto::HashType::FromHex));
        auto flag = bcos::ledger::HEADER | bcos::ledger::RECEIPTS;
        flag |= fullTransaction ? bcos::ledger::TRANSACTIONS : bcos::ledger::TRANSACTIONS_HASH;
        auto block = co_await ledger::getBlockData(*ledger, number, flag);
        combineBlockResponse(result, *block, fullTransaction);
    }
    catch (std::exception const& e)
    {
        WEB3_LOG(DEBUG) << "getBlockByHash failed: " << boost::diagnostic_information(e);
        result = Json::nullValue;
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getBlockByNumber(const Json::Value& request, Json::Value& response)
{
    // params: blockNumber(QTY|TAG), fullTransaction(Boolean)
    // result: block(BLOCK)
    auto const blockTag = toView(request[0U]);
    auto const fullTransaction = request[1U].asBool();
    Json::Value result = Json::objectValue;
    try
    {
        auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
        auto const ledger = m_nodeService->ledger();
        auto flag = bcos::ledger::HEADER | bcos::ledger::RECEIPTS;
        flag |= fullTransaction ? bcos::ledger::TRANSACTIONS : bcos::ledger::TRANSACTIONS_HASH;
        auto block = co_await ledger::getBlockData(*ledger, blockNumber, flag);
        combineBlockResponse(result, *block, fullTransaction);
    }
    catch (std::exception const& e)
    {
        WEB3_LOG(DEBUG) << "getBlockByNumber failed: " << boost::diagnostic_information(e);
        result = Json::nullValue;
    }
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getTransactionByHash(
    const Json::Value& request, Json::Value& response)
{
    // params: transactionHash(DATA)
    // result: transaction(TX)
    auto const txHash = toView(request[0U]);
    auto const hash = crypto::HashType(txHash, crypto::HashType::FromHex);
    auto hashList = std::make_shared<crypto::HashList>();
    hashList->push_back(hash);
    auto const ledger = m_nodeService->ledger();
    Json::Value result = Json::objectValue;
    try
    {
        auto const txs = co_await ledger::getTransactions(*ledger, std::move(hashList));
        auto receipt = co_await ledger::getReceipt(*ledger, hash);
        if (!receipt || !txs || txs->empty())
        {
            result = Json::nullValue;
            buildJsonContent(result, response);
            co_return;
        }
        auto blockHash = co_await ledger::getBlockHash(*ledger, receipt->blockNumber());
        combineTxResponse(result, *txs->at(0), *receipt, blockHash);
    }
    catch (std::exception const& e)
    {
        WEB3_LOG(DEBUG) << "getTransactionByHash failed: " << boost::diagnostic_information(e);
        result = Json::nullValue;
    }
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::getTransactionByBlockHashAndIndex(
    const Json::Value& request, Json::Value& response)
{
    // params: blockHash(DATA), transactionIndex(QTY)
    // result: transaction(TX)
    auto const blockHash = toView(request[0U]);
    auto const transactionIndex = fromQuantity(std::string(toView(request[1U])));
    auto const hash = crypto::HashType(blockHash, crypto::HashType::FromHex);
    auto const ledger = m_nodeService->ledger();
    Json::Value result = Json::objectValue;
    auto const number = co_await ledger::getBlockNumber(*ledger, hash);
    // will not throw exception in getBlockNumber if not found
    if (number <= 0) [[unlikely]]
    {
        result = Json::nullValue;
        buildJsonContent(result, response);
        co_return;
    }
    auto block = co_await ledger::getBlockData(
        *ledger, number, bcos::ledger::TRANSACTIONS | bcos::ledger::HEADER);
    if (!block || transactionIndex >= block->transactionsSize()) [[unlikely]]
    {
        result = Json::nullValue;
        buildJsonContent(result, response);
        co_return;
    }
    auto transactions = block->transactions();
    auto tx = transactions.at(transactionIndex);
    auto receipt = co_await ledger::getReceipt(*ledger, tx->hash());
    combineTxResponse(result, *tx, *receipt, hash);
    buildJsonContent(result, response);
}

task::Task<void> EthEndpoint::getTransactionByBlockNumberAndIndex(
    const Json::Value& request, Json::Value& response)
{
    // params: blockNumber(QTY|TAG), transactionIndex(QTY)
    // result: transaction(TX)
    auto const blockTag = toView(request[0U]);
    auto const transactionIndex = fromQuantity(std::string(toView(request[1U])));
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    auto const ledger = m_nodeService->ledger();
    Json::Value result = Json::objectValue;
    try
    {
        auto block = co_await ledger::getBlockData(
            *ledger, blockNumber, bcos::ledger::TRANSACTIONS_HASH | bcos::ledger::HEADER);
        if (!block || transactionIndex >= block->transactionsMetaDataSize()) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid transaction index!"));
        }
        auto txHashes = block->transactionMetaDatas();
        auto txHash = txHashes[transactionIndex]->hash();
        auto txList = std::make_shared<crypto::HashList>();
        txList->emplace_back(txHash);
        auto tx = co_await ledger::getTransactions(*ledger, std::move(txList));
        if (tx->empty())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid transaction index!"));
        }
        auto receipt = co_await ledger::getReceipt(*ledger, txHash);
        auto blockHash = block->blockHeader()->hash();
        combineTxResponse(result, *(*tx)[0], *receipt, blockHash);
    }
    catch (std::exception const& e)
    {
        WEB3_LOG(DEBUG) << "getTransactionByBlockNumberAndIndex failed: "
                        << boost::diagnostic_information(e);
        result = Json::nullValue;
    }
    buildJsonContent(result, response);
}

task::Task<void> EthEndpoint::getTransactionReceipt(
    const Json::Value& request, Json::Value& response)
{
    // params: transactionHash(DATA)
    // result: transactionReceipt(RECEIPT)
    auto const hashStr = toView(request[0U]);
    auto const hash = crypto::HashType(hashStr, crypto::HashType::FromHex);
    auto const ledger = m_nodeService->ledger();
    Json::Value result = Json::objectValue;
    try
    {
        auto receipt = co_await ledger::getReceipt(*ledger, hash);
        auto hashList = std::make_shared<crypto::HashList>();
        hashList->push_back(hash);
        auto txs = co_await ledger::getTransactions(*ledger, std::move(hashList));
        if (!receipt || !txs || txs->empty())
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "Invalid transaction hash: " + hash.hexPrefixed()));
        }
        auto blockHash = co_await ledger::getBlockHash(*ledger, receipt->blockNumber());
        combineReceiptResponse(result, *receipt, *txs->at(0), blockHash);
    }
    catch (std::exception const& e)
    {
        WEB3_LOG(DEBUG) << "getTransactionReceipt failed: " << boost::diagnostic_information(e);
        result = Json::nullValue;
        buildJsonContent(result, response);
    }
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::getUncleByBlockHashAndIndex(const Json::Value&, Json::Value& response)
{
    Json::Value result = "null";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::getUncleByBlockNumberAndIndex(
    const Json::Value&, Json::Value& response)
{
    Json::Value result = "null";
    buildJsonContent(result, response);
    co_return;
}
task::Task<void> EthEndpoint::newFilter(const Json::Value& request, Json::Value& response)
{
    // params: filter(FILTER)
    // result: filterId(QTY)
    const Json::Value& jParams = request[0U];
    auto const ledger = m_nodeService->ledger();
    auto const latest = co_await ledger::getCurrentBlockNumber(*ledger);
    auto params = m_filterSystem->requestFactory()->create();
    params->fromJson(
        jParams, latest, m_nodeService->safeBlockDepth(), m_nodeService->finalizedBlockDepth());
    Json::Value result = co_await m_filterSystem->newFilter(params);
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::newBlockFilter(const Json::Value&, Json::Value& response)
{
    // result: filterId(QTY)
    Json::Value result = co_await m_filterSystem->newBlockFilter();
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::newPendingTransactionFilter(const Json::Value&, Json::Value& response)
{
    // result: filterId(QTY)
    Json::Value result = co_await m_filterSystem->newPendingTxFilter();
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::uninstallFilter(const Json::Value& request, Json::Value& response)
{
    // params: filterId(QTY)
    // result: success(Boolean)
    auto const id = fromBigQuantity(toView(request[0U]));
    Json::Value result = co_await m_filterSystem->uninstallFilter(id);
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::getFilterChanges(const Json::Value& request, Json::Value& response)
{
    // params: filterId(QTY)
    // result: logs(ARRAY)
    auto const id = fromBigQuantity(toView(request[0U]));
    Json::Value result = co_await m_filterSystem->getFilterChanges(id);
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::getFilterLogs(const Json::Value& request, Json::Value& response)
{
    // params: filterId(QTY)
    // result: logs(ARRAY)
    auto const id = fromBigQuantity(toView(request[0U]));
    Json::Value result = co_await m_filterSystem->getFilterLogs(id);
    buildJsonContent(result, response);
}
task::Task<void> EthEndpoint::getLogs(const Json::Value& request, Json::Value& response)
{
    // params: filter(FILTER)
    // result: logs(ARRAY)
    const Json::Value& jParams = request[0U];
    auto const ledger = m_nodeService->ledger();
    auto const latest = co_await ledger::getCurrentBlockNumber(*ledger);
    auto params = m_filterSystem->requestFactory()->create();
    params->fromJson(
        jParams, latest, m_nodeService->safeBlockDepth(), m_nodeService->finalizedBlockDepth());
    Json::Value result = co_await m_filterSystem->getLogs(params);
    buildJsonContent(result, response);
}
task::Task<std::tuple<protocol::BlockNumber, bool>> EthEndpoint::getBlockNumberByTag(
    std::string_view blockTag)
{
    auto ledger = m_nodeService->ledger();
    auto latest = co_await ledger::getCurrentBlockNumber(*ledger);
    auto [number, _] = bcos::rpc::getBlockNumberByTag(
        latest, blockTag, m_nodeService->safeBlockDepth(), m_nodeService->finalizedBlockDepth());
    co_return std::make_tuple(number, std::cmp_equal(latest, number));
}

task::Task<void> EthEndpoint::maxPriorityFeePerGas(
    const Json::Value& request, Json::Value& response)
{
    Json::Value result = "0x0";
    buildJsonContent(result, response);
    co_return;
}

/// eth_getProof custom error code (spec §5.9): both request-level proof failures — dormant
/// account and unknown/uncommitted state root — map to -32004; the message distinguishes them.
constexpr int32_t EthGetProofUnavailable = -32004;

task::Task<void> EthEndpoint::getProof(const Json::Value& request, Json::Value& response)
{
    // params: address(DATA 20B), storageKeys(DATA[] of 32B), blockNumber(QTY|TAG)  (EIP-1186)
    // result: {address, balance, nonce, codeHash, storageHash, accountProof[], storageProof[]}
    Address address;
    try
    {
        address = Address(toView(request[0U]), Address::FromHex, Address::AlignRight);
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid address"));
    }
    auto const& keysJson = request[1U];
    if (!keysJson.isNull() && !keysJson.isArray())
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "storageKeys must be an array"));
    }
    std::vector<h256> slots;
    slots.reserve(keysJson.size());
    for (auto const& key : keysJson)
    {
        try
        {
            slots.emplace_back(toView(key), h256::FromHex, h256::AlignRight);
        }
        catch (...)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid storage key"));
        }
    }
    auto const blockTag = toView(request[2U]);
    auto [blockNumber, _] = co_await getBlockNumberByTag(blockTag);
    if (c_fileLogLevel == TRACE)
    {
        WEB3_LOG(TRACE) << "eth_getProof" << LOG_KV("address", address.hexPrefixed())
                        << LOG_KV("slotCount", slots.size()) << LOG_KV("blockTag", blockTag)
                        << LOG_KV("blockNumber", blockNumber);
    }

    // Resolve the block's stateRoot from its header.
    auto const ledger = m_nodeService->ledger();
    auto const block = co_await ledger::getBlockData(*ledger, blockNumber, bcos::ledger::HEADER);
    if (!block || !block->blockHeader()) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    auto const stateRoot = block->blockHeader()->stateRoot();

    // The MPT node reader is wired by the AIR initializer (AirNodeInitializer); unset means
    // this node has no local path to MPT node rows (e.g. a tars-built NodeService) — a
    // deployment matter, hence -32603 rather than -32004.
    auto const mptReader = m_nodeService->mptNodeReader();
    if (!mptReader) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, "MPT not enabled on this node"));
    }

    // The exclusion-vs-cold-slot distinction is mode-driven (spec §5.9): only under
    // feature_l2_ethereum_compat (scenario B) are the storage tries complete, making an
    // exclusion walk a provable zero. Otherwise (scenario A) the trie omits slots never
    // written after MPT activation, and generateProof marks such slots inMPT=false instead
    // of emitting a lying value-0 exclusion proof. Single-flag read (one SYS_CONFIG row,
    // same helper as resolveHistoricalMptContext); degrades to false (honest scenario-A
    // behavior) on fetch failure.
    auto const fullTrie = co_await ledger::getFeature(
        *ledger, ledger::Features::Flag::feature_l2_ethereum_compat, blockNumber);

    auto result = co_await ledger::mpt::generateProof(
        *mptReader, stateRoot, address, std::span<h256 const>(slots), fullTrie);
    if (auto const* errorCode = std::get_if<ledger::mpt::ProofErrorCode>(&result))
    {
        auto const* message = (*errorCode == ledger::mpt::ProofErrorCode::AccountNotInMPT) ?
                                  "Account not in trie (dormant in scenario A)" :
                                  "Block stateRoot not in MPT node storage";
        BOOST_THROW_EXCEPTION(JsonRpcException(EthGetProofUnavailable, message));
    }
    auto& proof = std::get<ledger::mpt::EIP1186Proof>(result);

    Json::Value output = Json::objectValue;
    output["address"] = address.hexPrefixed();
    output["balance"] = toQuantity(proof.balance);
    output["nonce"] = toQuantity(proof.nonce);
    output["codeHash"] = proof.codeHash.hexPrefixed();
    output["storageHash"] = proof.storageHash.hexPrefixed();
    Json::Value accountProof = Json::arrayValue;
    for (auto const& node : proof.accountProof)
    {
        accountProof.append(toHexStringWithPrefix(node));
    }
    output["accountProof"] = std::move(accountProof);
    Json::Value storageProof = Json::arrayValue;
    auto const addressHex = address.hex();  // ledger::getStorageAt's shape: lowercase, unprefixed
    for (auto& entry : proof.storageProof)
    {
        Json::Value entryJson = Json::objectValue;
        entryJson["key"] = entry.key.hexPrefixed();
        if (!entry.inMPT)
        {
            // SlotNotInMPT (spec §5.9): the slot is absent from the scenario-A storage trie, so
            // no Merkle proof exists — "value" is the authoritative flat-KV truth, "proof" the
            // empty array. Forward the resolved blockNumber (not a hardcoded 0 like the other
            // Web3 flat reads): unlike them, this endpoint's Merkle half DOES honor blockTag —
            // it proves against the requested block's stateRoot — so the flat half must target
            // the same block. Ledger::getStorageAt ignores the argument today and serves
            // latest-committed state; passing it keeps this call site correct once historical
            // flat reads land, instead of silently staying latest-only. Unset slot reads as zero.
            std::string quantity = "0x0";
            if (auto const flat = co_await ledger::getStorageAt(
                    *ledger, addressHex, entry.key.toRawString(), blockNumber);
                flat.has_value())
            {
                quantity = toQuantity(flat.value().get());
            }
            entryJson["value"] = std::move(quantity);
            entryJson["proof"] = Json::arrayValue;
            entryJson["inMPT"] = false;
            storageProof.append(std::move(entryJson));
            continue;
        }
        // The trie leaf stores RLP(big-endian-trimmed slot value); EIP-1186 "value" is the
        // slot's QUANTITY. Strip the RLP string header to recover the payload bytes and render
        // them as a quantity — 0x0 when the slot is absent (empty leaf value).
        bcos::bytes payload;
        if (!entry.value.empty())
        {
            auto valueRef = bcos::ref(entry.value);
            if (auto error = codec::rlp::decode(valueRef, payload); error != nullptr) [[unlikely]]
            {
                BOOST_THROW_EXCEPTION(
                    JsonRpcException(InternalError, "Malformed storage leaf RLP"));
            }
        }
        entryJson["value"] = toQuantity(payload);
        Json::Value proofJson = Json::arrayValue;
        for (auto const& node : entry.proof)
        {
            proofJson.append(toHexStringWithPrefix(node));
        }
        entryJson["proof"] = std::move(proofJson);
        entryJson["inMPT"] = true;
        storageProof.append(std::move(entryJson));
    }
    output["storageProof"] = std::move(storageProof);
    buildJsonContent(output, response);
}

bcos::rpc::EthEndpoint::EthEndpoint(
    NodeService::Ptr nodeService, FilterSystem::Ptr filterSystem, bool syncTransaction)
  : m_nodeService(std::move(nodeService)),
    m_filterSystem(std::move(filterSystem)),
    m_syncTransaction(syncTransaction)
{}
