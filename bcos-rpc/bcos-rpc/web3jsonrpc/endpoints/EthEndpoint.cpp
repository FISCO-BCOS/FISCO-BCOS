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
#include <bcos-ledger/mpt/Classify.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Proof.h>
#include <bcos-ledger/mpt/history/HistoryErrors.h>
#include <bcos-ledger/mpt/history/HistoryRead.h>
#include <bcos-ledger/mpt/history/MPTHistory.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-rpc/Common.h>
#include <bcos-rpc/util.h>
#include <bcos-rpc/web3jsonrpc/model/BlockResponse.h>
#include <bcos-rpc/web3jsonrpc/model/CallRequest.h>
#include <bcos-rpc/web3jsonrpc/model/ReceiptResponse.h>
#include <bcos-rpc/web3jsonrpc/model/TransactionResponse.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tx-validator/TxValidator.h>
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
/// getCode): a height this node cannot serve from its state reverse history — older than the
/// retained window, no history retained at all, an era that was never recorded, or an address
/// whose rows the history does not cover (system contracts).
constexpr int32_t EthHistoricalStateUnavailable = -32004;

/// The plane a historical STATE query reads, plus the window it is allowed to read it in.
///
/// Historical state comes from the state reverse history, one row at a time (pathdb spec §11):
/// a path-addressed node store keeps ONE version per position, so the trie at an older header's
/// root is not a thing this node can walk, and the old `holdsTrieRoot` admission check rejected
/// every superseded root by construction. The index answers per key instead, with no dependence
/// on the trie's shape.
struct HistoricalStateContext
{
    /// The node's history object — borrowed, and kept alive for the request by the shared_ptr the
    /// NodeService holds. `history->state()` owns the in-memory index every row is located
    /// through; `history->backend()` is the plane its shard rows are read from.
    std::shared_ptr<ledger::mpt::history::MPTHistory> history;
    bcos::protocol::BlockNumber block{};
    bcos::protocol::BlockNumber tip{};
    bcos::protocol::BlockNumber depth{};
};

/// Admit a historical state query, or refuse it with the reason. Never falls back to the latest
/// state: every refusal below is a case where today's rows would look like a valid answer.
bcos::task::Task<HistoricalStateContext> resolveHistoricalStateContext(
    rpc::NodeService& nodeService, bcos::ledger::LedgerInterface& ledger,
    bcos::protocol::BlockNumber blockNumber)
{
    auto const block = co_await ledger::getBlockData(ledger, blockNumber, bcos::ledger::HEADER);
    if (!block || !block->blockHeader()) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    auto history = nodeService.mptHistory();
    if (!history || !history->backend()) [[unlikely]]
    {
        // A deployment matter (a tars-built NodeService has no local storage), not a request
        // one — hence -32603 rather than -32004.
        BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, "MPT not enabled on this node"));
    }
    auto const depths = history->depths();
    if (depths.state <= 0) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EthHistoricalStateUnavailable,
            "Historical state is not retained on this node "
            "(storage.mpt_history_state_blocks = 0)"));
    }
    auto const tip = co_await ledger::getCurrentBlockNumber(ledger);
    bool covered = false;
    try
    {
        covered = ledger::mpt::history::historyCoversBlock(history->state(), blockNumber, tip);
    }
    catch (ledger::mpt::history::HistoryPruned const&)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EthHistoricalStateUnavailable,
            fmt::format("Block {} is older than the retained state history window "
                        "(storage.mpt_history_state_blocks = {})",
                blockNumber, depths.state)));
    }
    catch (ledger::mpt::history::HistoryIndexUnavailable const&)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EthHistoricalStateUnavailable,
            "history index unavailable on this node (rebuild failed); see node log"));
    }
    if (!covered) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EthHistoricalStateUnavailable,
            fmt::format("No state history recorded for block {} on this node", blockNumber)));
    }
    co_return HistoricalStateContext{
        .history = std::move(history), .block = blockNumber, .tip = tip, .depth = depths.state};
}

/// The value one flat account row held at the context's block; nullopt when the row did not
/// exist then. HistoryUseCurrent means nothing changed the row since, so the committed CURRENT
/// row is that block's value — read from the same plane the window guard's tip describes, so a
/// not-yet-committed block cannot leak into a historical answer.
/// @param rowKey the MPT row name (Classify.h's ROW_BALANCE / ROW_NONCE / ROW_CODE_HASH), not
///        the executor's ACCOUNT_* spelling. The two are equal today, but the history captures
///        and classifies by the MPT names (HistoryCommit.h::isHistoricalStateRow ->
///        classifyRowKey), so a read that spelled them the other way would silently stop matching
///        what was recorded if either set ever moved.
bcos::task::Task<std::optional<std::string>> historicalStateRow(
    HistoricalStateContext const& context, std::string_view table, std::string_view rowKey)
{
    executor_v1::StateKeyView const keyView{table, rowKey};
    ledger::mpt::history::ReadAtResult version;
    try
    {
        version = co_await ledger::mpt::history::readStateAt(context.history->state(),
            *context.history->backend(), keyView, context.block, context.tip, context.depth);
    }
    catch (ledger::mpt::history::HistoryPruned const&)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EthHistoricalStateUnavailable,
            fmt::format(
                "Block {} is older than the retained state history window", context.block)));
    }
    catch (ledger::mpt::history::HistoryIndexUnavailable const&)
    {
        // The admission check above passed, so the index went unusable mid-request — a publish
        // that threw. Refusing is the only reading left: a Ready answer and an Unavailable one
        // differ exactly in whether a missing version means "unchanged" (G10).
        BOOST_THROW_EXCEPTION(JsonRpcException(EthHistoricalStateUnavailable,
            "history index unavailable on this node (rebuild failed); see node log"));
    }
    if (auto* recorded = std::get_if<bcos::bytes>(std::addressof(version)))
    {
        co_return std::string(recorded->begin(), recorded->end());
    }
    if (std::holds_alternative<ledger::mpt::history::HistoryAbsent>(version))
    {
        co_return std::nullopt;
    }
    auto const current = co_await bcos::storage2::readOne(
        *context.history->backend(), executor_v1::StateKey{keyView});
    if (!current)
    {
        co_return std::nullopt;
    }
    co_return std::string(current->get());
}

/// The account table a historical state read must look in — or a refusal, when there is none.
///
/// EVMAccount puts system-contract addresses under "/sys/" and user accounts under "/apps/", and
/// the latest branch of each endpoint selects between them. The state history has no such
/// choice: it captures exactly the rows `Classify.h::parseAccountTable` recognises, which is
/// "/apps/<40-hex>" and nothing else, because that is the set MPTBuilder folds into the Ethereum
/// commitment. A system contract's rows were therefore never recorded at any height.
///
/// So the two sides are made consistent by REFUSING rather than by widening the table: looking a
/// system-contract address up under "/apps/" would find nothing and answer 0 / 0 / "0x" / a zero
/// word for state that demonstrably exists, which is the fabricated answer G6 forbids. Widening
/// the read to "/sys/" would be worse — those rows have no pre-images, so it would silently
/// serve TODAY's system-contract state under an old block's number.
std::string historicalAccountTable(std::string const& addressStr)
{
    // Normalize to the 40-char form FIRST, then test membership. The eth_ endpoints reach here
    // with whatever the caller sent, minus "0x" and lowercased — a short form like "1000" is
    // legal JSON-RPC input — while c_systemTxsAddress holds 40-char forms. Testing the raw
    // string would let "0x1000" slip past the refusal below and be answered from
    // "/apps/00…001000", a table with no rows: the fabricated zero this function exists to
    // prevent, reached by the one input that looks least like a system address.
    bcos::Address const address{addressStr, bcos::Address::FromHex, bcos::Address::AlignRight};
    auto const normalized = address.hex();  // 40 lowercase hex, no prefix
    if (precompiled::contains(bcos::precompiled::c_systemTxsAddress, std::string_view{normalized}))
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EthHistoricalStateUnavailable,
            "Historical state is not retained for system-contract addresses: the state history "
            "records only /apps/ account rows"));
    }
    return ledger::mpt::accountTableName(address);
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
        // Historical state: the account's balance row as of that block, from the state reverse
        // history (pathdb spec §11). A row that did not exist then reads as 0 — the same
        // absent-account semantics the latest path above uses, and no longer scenario-dependent:
        // the index records what the flat row held, so there is no "dormant, invisible to an
        // incomplete trie" case left to distinguish.
        auto const ctx =
            co_await resolveHistoricalStateContext(*m_nodeService, *ledger, blockNumber);
        if (auto const row = co_await historicalStateRow(
                ctx, historicalAccountTable(addressStr), ledger::mpt::ROW_BALANCE))
        {
            balance = u256(*row);
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

    // Historical state: the slot's own flat row as of that block, from the state reverse history
    // (pathdb spec §11 puts historical state on StateIndex, not on a rooted trie walk).
    //
    // What changed with it, and it is a real semantic change: absence no longer has two readings.
    // The MPT path had to tell "absent because the account/slot genuinely held nothing" from
    // "absent because a scenario-A trie never committed this slot", and answered the second with
    // an error or with a flat fallback that ignored the requested height. The index records what
    // the ROW held, whatever the trie did with it, so an absent row at block N means the slot was
    // unset at block N — on either scenario — and reads as zero, like the latest path.
    auto const ctx = co_await resolveHistoricalStateContext(*m_nodeService, *ledger, blockNumber);
    auto const slotRow = co_await historicalStateRow(
        ctx, historicalAccountTable(addressStr), positionBytes.toRawString());
    // Render like the flat path: a fixed-width 32-byte hex value; an unset slot is 32 zero bytes.
    Json::Value result = slotRow ? storageValueToData(*slotRow) : storageValueToData({});
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
        // Historical state: the account's nonce row as of that block, from the state reverse
        // history. Absent reads as 0, the same absent-account answer the latest path gives.
        auto const ctx =
            co_await resolveHistoricalStateContext(*m_nodeService, *ledger, blockNumber);
        if (auto const row = co_await historicalStateRow(
                ctx, historicalAccountTable(addressStr), ledger::mpt::ROW_NONCE))
        {
            nonce = u256(*row);
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
        // The codeHash row as of that block comes from the state reverse history; the code
        // BYTES do not need a historical plane at all — s_code_binary is content-addressed and
        // append-only, so the bytes under a given hash are the same at any height.
        auto const ctx =
            co_await resolveHistoricalStateContext(*m_nodeService, *ledger, blockNumber);
        auto const codeHashRow = co_await historicalStateRow(
            ctx, historicalAccountTable(addressStr), ledger::mpt::ROW_CODE_HASH);
        if (codeHashRow && !codeHashRow->empty())
        {
            auto const codeHash = bcos::h256(
                bcos::bytesConstRef(
                    reinterpret_cast<bcos::byte const*>(codeHashRow->data()), codeHashRow->size()),
                bcos::h256::AlignLeft);
            if (codeHash != bcos::ledger::mpt::emptyCodeHash())
            {
                auto const stateStorage = ledger->getStateStorage();
                if (auto const codeEntry = co_await bcos::storage2::readOne(*stateStorage,
                        executor_v1::StateKeyView{bcos::ledger::SYS_CODE_BINARY, *codeHashRow});
                    codeEntry.has_value())
                {
                    code.assign(codeEntry.value().get().begin(), codeEntry.value().get().end());
                }
            }
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
    // Reject blob txs at the RPC gate. Deposits (0x7e) enter only via Engine API.
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
        auto validator = m_nodeService->admissionValidator();
        if (!validator) [[unlikely]]
        {
            // The wiring that set the mempool sets this too. Refuse rather than admit
            // unchecked: this branch used to carry its own chainId and signature checks, and
            // silently dropping them is how a node ends up accepting transactions signed for
            // another chain.
            BOOST_THROW_EXCEPTION(
                JsonRpcException(JsonRpcError::InternalError, "admission validator not available"));
        }
        // Required, always: this transaction was built directly above, so m_tainted is still
        // true and no signature has been verified. tryAdd refuses a tainted transaction, and
        // the Signature check is what clears it.
        if (auto status = co_await validator->verify(
                *tx, m_nodeService->admissionContext(), txvalidator::SignaturePolicy::Required);
            status != protocol::TransactionStatus::None)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, protocol::toString(status)));
        }
        // tryAdd, not add: add() returns void and ends four different ways without saying so,
        // and this method answers with a transaction hash as soon as it returns. It is also
        // what reserves the (sender, nonce) pair -- checking first and adding second would let
        // two concurrent submissions through the gap between the two calls.
        if (auto status = memPool->tryAdd(std::move(tx));
            status != protocol::TransactionStatus::None)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, protocol::toString(status)));
        }
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
    auto [blockNumber, isLatest] = co_await getBlockNumberByTag(blockTag);
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
    // of emitting a lying value-0 exclusion proof. Single-flag read: one SYS_CONFIG row rather
    // than fetchAllFeatures' ~60-key scan; degrades to false (honest scenario-A behavior) on
    // fetch failure.
    auto const fullTrie = co_await ledger::getFeature(
        *ledger, ledger::Features::Flag::feature_l2_ethereum_compat, blockNumber);

    std::variant<ledger::mpt::EIP1186Proof, ledger::mpt::ProofErrorCode> result;
    if (isLatest)
    {
        result = co_await ledger::mpt::generateProof(
            *mptReader, stateRoot, address, std::span<h256 const>(slots), fullTrie);
    }
    else
    {
        // A past block: the node rows hold ONE version per position, so the walk is served from
        // the trie-node reverse history instead — each position resolved to its version at this
        // block (pathdb spec §10.2). The proof BYTES are the same object either way: the walk
        // still verifies every node against the hash its parent records, which is exactly what
        // makes a historical version trustworthy rather than merely plausible.
        auto const history = m_nodeService->mptHistory();
        if (!history || !history->backend() || history->depths().proof <= 0) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(EthGetProofUnavailable,
                "Historical proofs are not retained on this node "
                "(storage.mpt_history_proof_blocks = 0)"));
        }
        auto const depth = history->depths().proof;
        auto const tip = co_await ledger::getCurrentBlockNumber(*ledger);
        // Being inside the window is a claim about the retention PARAMETER; whether this node
        // ever recorded that era is a separate question, and without it every position would
        // resolve to HistoryUseCurrent — today's trie, proved against an old header's root,
        // which would simply fail the parent-child check and look like corruption.
        bool covered = false;
        try
        {
            covered = ledger::mpt::history::historyCoversBlock(history->trie(), blockNumber, tip);
        }
        catch (ledger::mpt::history::HistoryPruned const&)
        {
            // The store's own retention boundary says this height is already gone, which the
            // parameter window need not have known.
            BOOST_THROW_EXCEPTION(JsonRpcException(EthGetProofUnavailable,
                fmt::format("Block {} is older than the retained trie-node history window "
                            "(storage.mpt_history_proof_blocks = {})",
                    blockNumber, depth)));
        }
        catch (ledger::mpt::history::HistoryIndexUnavailable const&)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(EthGetProofUnavailable,
                "history index unavailable on this node (rebuild failed); see node log"));
        }
        if (!covered) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(EthGetProofUnavailable,
                fmt::format(
                    "No trie-node history recorded for block {} on this node", blockNumber)));
        }
        ledger::mpt::history::HistoricalNodeStorage<NodeService::MPTNodeReader,
            ledger::mpt::history::MPTHistory::Backend>
            historicalNodes(
                *mptReader, history->trie(), *history->backend(), blockNumber, tip, depth);
        try
        {
            result = co_await ledger::mpt::generateProof(
                historicalNodes, stateRoot, address, std::span<h256 const>(slots), fullTrie);
        }
        catch (ledger::mpt::history::HistoryPruned const&)
        {
            // The ONLY out-of-window outcome: the guard fires before the lookup, so this is never
            // confused with "the position never changed" (spec B.3).
            BOOST_THROW_EXCEPTION(JsonRpcException(EthGetProofUnavailable,
                fmt::format("Block {} is older than the retained trie-node history window "
                            "(storage.mpt_history_proof_blocks = {})",
                    blockNumber, depth)));
        }
        catch (ledger::mpt::history::HistoryIndexUnavailable const&)
        {
            // The index went unusable between admission and the walk (a publish that threw).
            BOOST_THROW_EXCEPTION(JsonRpcException(EthGetProofUnavailable,
                "history index unavailable on this node (rebuild failed); see node log"));
        }
    }
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
    // Resolved at most once for the whole request, so every cold slot below is read against ONE
    // tip snapshot rather than a per-slot one — two slots of the same proof must not straddle a
    // commit. Lazy, not hoisted outright: a proof whose slots are all in the trie needs no flat
    // half at all, and must not start failing on a node that retains no state history.
    std::optional<HistoricalStateContext> stateContext;
    for (auto& entry : proof.storageProof)
    {
        Json::Value entryJson = Json::objectValue;
        entryJson["key"] = entry.key.hexPrefixed();
        if (!entry.inMPT)
        {
            // SlotNotInMPT (spec §5.9): the slot is absent from the scenario-A storage trie, so
            // no Merkle proof exists — "value" is the authoritative flat-KV truth, "proof" the
            // empty array.
            //
            // The two halves of this response MUST describe the same height. The Merkle half
            // honours blockTag (it proves against the requested block's stateRoot), so the flat
            // half has to as well — and `ledger::getStorageAt` cannot do it: it takes a block
            // number and discards it (`Ledger.cpp`: `std::ignore = _blockNumber`), always
            // serving latest-committed state. Reading a cold slot through it at a past height
            // therefore returned TODAY's value inside a proof anchored at B, with nothing in
            // the response to say so.
            //
            // The historical flat read the rest of this file now uses is the answer: the state
            // reverse history holds this row's value at B. When the node cannot serve that
            // height, resolveHistoricalStateContext refuses with -32004 rather than letting the
            // latest value stand in — a refusal is the only honest answer for half a proof.
            std::string quantity = "0x0";
            std::optional<std::string> slotRow;
            if (isLatest)
            {
                if (auto const flat = co_await ledger::getStorageAt(
                        *ledger, addressHex, entry.key.toRawString(), blockNumber);
                    flat.has_value())
                {
                    slotRow = std::string(flat.value().get());
                }
            }
            else
            {
                if (!stateContext)
                {
                    stateContext = co_await resolveHistoricalStateContext(
                        *m_nodeService, *ledger, blockNumber);
                }
                slotRow = co_await historicalStateRow(
                    *stateContext, historicalAccountTable(addressHex), entry.key.toRawString());
            }
            if (slotRow)
            {
                quantity = toQuantity(*slotRow);
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
