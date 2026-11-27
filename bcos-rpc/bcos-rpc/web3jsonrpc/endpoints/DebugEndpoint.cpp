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
#include <bcos-framework/engine/RawTransactionDispatch.h>  // receiptLeafTypeByte
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/LegacyStorageMethods.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>  // encodeReceiptLeaf
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-rlp-protocol/EthBlockBody.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/EthWithdrawal.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/util.h>
#include <bcos-rpc/web3jsonrpc/utils/CanonicalReads.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/RpcChainPolicy.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-task/FireAwaitable.h>
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

/// EIP-2718 type byte of an OP Stack deposit transaction (0x7e). Spelled here rather than through
/// TransactionType because that enum name is declared in two headers with different members
/// (bcos::protocol's framework enum has no Deposit), so the name alone is ambiguous in this TU.
constexpr std::uint8_t c_depositTxType = 0x7e;

/// The full signed wire form of one stored transaction — the value its transactions-trie leaf
/// holds, so keccak256(result) is the canonical transaction hash.
///
/// Three storage shapes reach here; the reassembly helper discriminates the last two itself, so
/// this function does not have to:
///   * deposit (0x7e): the transaction has no signature at all; its signing preimage IS the full
///     envelope (Web3TxHandler's DepositTxHandler::encodeForSign returns encode()), and the helper
///     would reject the missing 65-byte trailer. Served verbatim here. Every OP L2 block carries an
///     L1-attributes deposit, so getting this wrong would make every OP block unservable.
///   * the POOL/ADMISSION carrier (takeToTarsTransaction, i.e. eth_sendRawTransaction): only the
///     signing preimage is stored, so the 65-byte r||s||yParity trailer is spliced back on.
///   * the ENGINE/OP carrier (decodedTransactionFromEnvelope, wrapOpP2pEnvelope): the payload is
///     already the full wire envelope — the exact bytes the OP lane hashes into txsRoot
///     (OpScheduler's rawTxBytes) — while the row still carries a signature.
/// reassembleWeb3RawTransaction tells the latter two apart by RLP item count (a typed body with
/// fields+3 items, a legacy body with a non-empty sealed trailer) and returns the wire form
/// verbatim after cross-checking that trailer against the stored signature, so calling it is
/// correct for both. Documented because the two shapes look identical byte-wise.
///
/// Returns an empty vector for a FISCO-native transaction, which has no EIP-2718 envelope at all,
/// and lets the helper's std::invalid_argument escape for a payload that is neither shape.
bcos::bytes fullRawEnvelope(bcos::protocol::Transaction const& transaction)
{
    auto const payload = transaction.extraTransactionBytes();
    if (payload.empty())
    {
        return {};
    }
    if (bcos::engine::rawTransactionTypeByte(payload) == c_depositTxType)
    {
        return bcos::bytes(payload.begin(), payload.end());
    }
    return bcostars::protocol::reassembleWeb3RawTransaction(payload, transaction.signatureData());
}


std::chrono::milliseconds traceTimeout(std::string_view text)
{
    auto invalid = []() -> void {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "timeout must be a duration between 1ms and 30s"));
    };
    if (text.empty() || text.size() > 128)
    {
        invalid();
    }
    if (text.front() == '+')
    {
        text.remove_prefix(1);
    }
    constexpr uint64_t c_maxNanoseconds = 30'000'000'000;
    constexpr uint64_t c_nanosecondsPerMillisecond = 1'000'000;
    uint64_t nanoseconds = 0;
    while (!text.empty())
    {
        uint64_t number = 0;
        bool digits = false;
        while (!text.empty() && text.front() >= '0' && text.front() <= '9')
        {
            digits = true;
            auto digit = static_cast<uint64_t>(text.front() - '0');
            if (number > (c_maxNanoseconds - digit) / 10)
            {
                invalid();
            }
            number = number * 10 + digit;
            text.remove_prefix(1);
        }
        std::string_view fractionDigits;
        if (!text.empty() && text.front() == '.')
        {
            text.remove_prefix(1);
            auto fractionStart = text;
            while (!text.empty() && text.front() >= '0' && text.front() <= '9')
            {
                digits = true;
                text.remove_prefix(1);
            }
            fractionDigits = fractionStart.substr(0, fractionStart.size() - text.size());
        }
        if (!digits)
        {
            invalid();
        }
        bool unitFound = false;
        for (auto const& [unit, scale] : {std::pair<std::string_view, uint64_t>{"ns", 1},
                 {"us", 1'000}, {"µs", 1'000}, {"μs", 1'000}, {"ms", 1'000'000},
                 {"s", 1'000'000'000}, {"m", 60'000'000'000}, {"h", 3'600'000'000'000}})
        {
            if (text.starts_with(unit))
            {
                if (number > (c_maxNanoseconds - nanoseconds) / scale)
                {
                    invalid();
                }
                nanoseconds += number * scale;
                // Work backwards to truncate each fractional component to nanoseconds
                // without floating-point boundary drift.
                uint64_t fraction = 0;
                for (auto it = fractionDigits.rbegin(); it != fractionDigits.rend(); ++it)
                {
                    fraction = (fraction + static_cast<uint64_t>(*it - '0') * scale) / 10;
                }
                if (fraction > c_maxNanoseconds - nanoseconds)
                {
                    invalid();
                }
                nanoseconds += fraction;
                text.remove_prefix(unit.size());
                unitFound = true;
                break;
            }
        }
        if (!unitFound)
        {
            invalid();
        }
    }
    if (nanoseconds < c_nanosecondsPerMillisecond)
    {
        invalid();
    }
    return std::chrono::milliseconds(
        (nanoseconds + c_nanosecondsPerMillisecond - 1) / c_nanosecondsPerMillisecond);
}

struct TraceJsonBudget
{
    std::chrono::steady_clock::time_point deadline;
    size_t bytes = 0;
    size_t frames = 0;

    void check() const
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(Web3DefaultError, "execution timeout"));
        }
        if (bytes > 8 * 1024 * 1024 || frames > 10000)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(Web3DefaultError, "trace output limit exceeded"));
        }
    }

    void account(std::string const& text)
    {
        for (size_t i = 0; i < text.size(); ++i)
        {
            auto c = static_cast<unsigned char>(text[i]);
            // Bound JSON escaping as well as raw data, before constructing the JSON tree.
            bytes += (c < 0x20 || c >= 0x80) ? 6 : (c == '"' || c == '\\') ? 2 : 1;
            if (i % 4096 == 0)
            {
                check();
            }
        }
        check();
    }
};

Json::Value traceJson(scheduler::CallTrace const& frame, TraceJsonBudget& budget, size_t depth = 0)
{
    if (depth > 1024)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Web3DefaultError, "trace depth limit exceeded"));
    }
    ++budget.frames;
    budget.bytes += 256;  // Field names, quotes, separators and the response envelope.
    budget.check();
    for (auto const* value : {&frame.type, &frame.from, &frame.to, &frame.value, &frame.gas,
             &frame.gasUsed, &frame.input, &frame.output, &frame.error, &frame.revertReason})
    {
        budget.account(*value);
    }
    Json::Value result(Json::objectValue);
    result["type"] = frame.type;
    result["from"] = frame.from;
    result["gas"] = frame.gas;
    result["gasUsed"] = frame.gasUsed;
    result["input"] = frame.input;
    for (auto const& [key, value] :
        {std::pair{"to", &frame.to}, {"value", &frame.value}, {"output", &frame.output},
            {"error", &frame.error}, {"revertReason", &frame.revertReason}})
    {
        if (!value->empty())
        {
            result[key] = *value;
        }
    }
    if (!frame.calls.empty())
    {
        result["calls"] = Json::Value(Json::arrayValue);
        for (auto const& child : frame.calls)
        {
            result["calls"].append(traceJson(*child, budget, depth + 1));
        }
    }
    return result;
}

// True iff this failure means "the ledger has no such row", as opposed to a read that failed.
//
// batchGetTransactions skips entries the storage reports as absent and then fails the batch with a
// count mismatch (Ledger.cpp: CollectAsyncCallbackError, "transactions size not match hashesSize").
// getBatchTxsByHashList wraps that as GetStorageError("GetBatchTxsByHashList error"), so the
// OUTER error carries no distinguishing information -- a genuine storage failure is wrapped in the
// same code and message. The decision therefore has to walk the cause chain for the count-mismatch
// leaf; anything else is a real failure and must not be flattened into "not found" (review F1).
bool isMissingTransactionRow(bcos::Error const& error)
{
    // The chain is at most a few links deep; the bound is only there so a malformed/cyclic cause
    // could never spin this loop.
    constexpr int c_maxCauseDepth = 8;
    auto const* current = &error;
    for (int depth = 0; current != nullptr && depth < c_maxCauseDepth; ++depth)
    {
        if (current->errorCode() == bcos::ledger::LedgerError::CollectAsyncCallbackError &&
            current->errorMessage().find("transactions size not match hashesSize") !=
                std::string::npos)
        {
            return true;
        }
        auto const* previous = boost::get_error_info<bcos::Error::PrevError>(*current);
        current = previous;
    }
    return false;
}

// Reads one transaction by hash, mapping "the ledger has no such row" to an empty result.
//
// The ledger's batch read fails closed: an absent SYS_HASH_2_TX row is reported by failing the
// batch with a count mismatch. Letting that escape this coroutine makes the dispatcher report an
// internal error, whereas geth answers a plain -32000 "not found" for the same input. Keeping the
// translation next to the co_await (rather than a try/catch at the call site) makes the
// exception->value mapping impossible to lose to coroutine frame teardown.
//
// Only the verified missing-row shape is translated. A storage or decode failure propagates with
// its own diagnostic and reaches the client as an internal error: answering "not found" for a
// broken read would tell kona-host to retry forever against a node that cannot serve the data.
task::Task<bcos::protocol::Transaction::Ptr> readTransactionOrNothing(
    bcos::ledger::LedgerInterface& ledger, std::shared_ptr<crypto::HashList> hashes)
{
    try
    {
        auto txs = co_await bcos::ledger::getTransactions(ledger, std::move(hashes));
        // An empty answer is the ledger saying the row is not there; it is not an error.
        co_return txs && !txs->empty() ? txs->at(0) : nullptr;
    }
    catch (bcos::Error const& e)
    {
        if (isMissingTransactionRow(e))
        {
            WEB3_LOG(DEBUG) << LOG_DESC("debug_getRawTransaction: transaction not in the ledger")
                            << LOG_KV("detail", e.errorMessage());
            co_return nullptr;
        }
        WEB3_LOG(WARNING) << LOG_DESC("debug_getRawTransaction: ledger read failed")
                          << LOG_KV("detail", e.what());
        throw;
    }
}
}  // namespace

task::Task<void> DebugEndpoint::traceTransaction(const Json::Value& request, Json::Value& response)
{
    if (!request.isArray() || request.size() != 2 || !request[0U].isString() ||
        !request[1U].isObject())
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams,
            "debug_traceTransaction expects a transaction hash and callTracer options"));
    }
    auto const hashText = request[0U].asString();
    if (hashText.size() != 66 || !hashText.starts_with("0x") ||
        hashText.find_first_not_of("0123456789abcdefABCDEF", 2) != std::string::npos)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid transaction hash"));
    }
    auto const& config = request[1U];
    if (!config["tracer"].isString() || config["tracer"].asString() != "callTracer")
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Only callTracer is supported"));
    }
    for (auto const& name : config.getMemberNames())
    {
        if (name != "tracer" && name != "timeout" && name != "tracerConfig")
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "Unsupported trace option: " + name));
        }
    }
    scheduler::TraceOptions options;
    if (config.isMember("timeout"))
    {
        if (!config["timeout"].isString())
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "timeout must be a duration string"));
        }
        options.timeout = traceTimeout(config["timeout"].asString());
    }
    if (config.isMember("tracerConfig"))
    {
        auto const& tracerConfig = config["tracerConfig"];
        if (!tracerConfig.isObject())
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "tracerConfig must be an object"));
        }
        for (auto const& name : tracerConfig.getMemberNames())
        {
            if (name != "onlyTopCall" || !tracerConfig[name].isBool())
            {
                BOOST_THROW_EXCEPTION(JsonRpcException(
                    InvalidParams, "tracerConfig supports only boolean onlyTopCall"));
            }
        }
        options.onlyTopCall = tracerConfig.get("onlyTopCall", false).asBool();
    }
    TraceJsonBudget budget{std::chrono::steady_clock::now() + options.timeout};
    auto executor = m_nodeService->scheduler();
    if (!executor)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, "Trace scheduler unavailable"));
    }
    auto pending = task::makeFireAwaitable<Error::Ptr, std::shared_ptr<scheduler::CallTrace>>(
        [executor, hash = crypto::HashType(hashText, crypto::HashType::FromHex), options](
            auto completion) {
            auto shared = std::make_shared<decltype(completion)>(std::move(completion));
            executor->traceTransaction(hash, options,
                [shared](Error::Ptr error, std::shared_ptr<bcos::scheduler::CallTrace> result) {
                    (*shared)(std::move(error), std::move(result));
                });
        },
        BCOS_ERROR_PTR(-1, "Trace execution cancelled"));
    auto [error, result] = co_await pending;
    if (error)
    {
        auto const code = error->errorCode() == scheduler::SchedulerError::MPTStateUnavailable ?
                              static_cast<int32_t>(ServiceNotInitCompleted) :
                              static_cast<int32_t>(Web3DefaultError);
        BOOST_THROW_EXCEPTION(JsonRpcException(code, error->errorMessage()));
    }
    if (!result)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InternalError, "Trace execution returned no result"));
    }
    auto json = traceJson(*result, budget);
    budget.check();
    buildJsonContent(json, response);
}

task::Task<void> DebugEndpoint::dbGet(const Json::Value& request, Json::Value& response)
{
    // params: key (DATA); result: the stored preimage (DATA)
    // kona-host (bin/host/src/single/handler.rs) sends two key shapes: geth hashdb's code key
    // 'c' || codeHash (33 bytes) and the bare 32-byte keccak hash (trie nodes). Both resolve
    // against the two keccak-addressed stores: MPT nodes (mptNodeReader, "/mpt/" rows) and
    // bytecode (s_code_binary). Content addressing makes either store a valid answer for either
    // shape; the shape only picks which store to try first. A miss answers -32000 "not found"
    // (geth's server-error code); kona-host then retries its code hint with the bare-hash form.
    co_await requireEthereumLane(*m_nodeService->ledger(), "debug_dbGet");
    if (request.size() < 1 || !request[0U].isString())
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "debug_dbGet expects one hex key"));
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

task::Task<protocol::Block::Ptr> DebugEndpoint::rawBlockByParameter(const Json::Value& request,
    std::string_view method, int32_t blockFlag, std::optional<crypto::HashType>* requestedHash)
{
    // One block parameter, shared by all raw-data endpoints: a block number, a tag, or a 32-byte
    // block hash (which kona-host uses for preimages). The read goes through the canonical plane
    // (CanonicalReads.h) rather than the raw ledger: on the OP lane the backend only holds
    // finalized blocks, and the head op-node is serving lives in the OpScheduler window.
    if (request.size() < 1 || !request[0U].isString())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, std::string(method) + " expects one block parameter"));
    }
    auto const blockTag = toView(request[0U]);

    // The safe/finalized/pending tags are refused: resolving them here with the static depth
    // (default 0) would answer the unsafe tip, contradicting eth_getBlockByNumber on the same
    // node (which fails closed on the engine lane when the forkchoice tracker is unset). A
    // block number, a hash, and the latest / earliest tags are accepted, through the same
    // resolver eth_getBlockByNumber uses -- on the fault-proof data plane one node must not
    // answer two different blocks for the same tag.
    if (blockTag == bcos::rpc::SafeBlock || blockTag == bcos::rpc::FinalizedBlock ||
        blockTag == bcos::rpc::PendingBlock)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams,
            std::string(method) + " does not accept the safe/finalized/pending tags; use a block "
                                  "number, a block hash, latest or earliest"));
    }

    // A hash names a block directly, and on the OP lane that includes an unfinalized window block
    // on any branch: reading it back by hash (not by the height its index row happens to hold)
    // means a reorg between resolve and read cannot swap in a different block. The caller still
    // re-checks the served identity against this hash, which turns a deferred replacement into
    // "not found" rather than a wrong answer.
    if (isBlockHashString(blockTag))
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
        if (requestedHash != nullptr)
        {
            *requestedHash = hash;
        }
        try
        {
            co_return co_await bcos::rpc::canonicalBlockByHash(*m_nodeService, hash, blockFlag);
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

    // QTY|TAG on the OP lane: capture the head ONCE and read the block out of that same view.
    // Resolving the tag through canonicalLatestNumber() and then calling canonicalBlockByNumber()
    // would take two independent snapshots, so a head rewind in between would either answer a
    // height the second view no longer has (a spurious "not found") or a block from a view that
    // does not belong to the number just resolved. One capture makes the answer coherent.
    if (auto const& reader = m_nodeService->opCanonicalReader())
    {
        auto view = co_await reader->captureHeadView();
        auto [number, _] =
            bcos::rpc::getBlockNumberByTag(view.head, blockTag, m_nodeService->safeBlockDepth(),
                m_nodeService->finalizedBlockDepth(), std::nullopt, std::nullopt, false);
        if (number < 0 || number > view.head)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
        }
        try
        {
            co_return co_await bcos::rpc::blockInView(view, number, blockFlag);
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

    // QTY|TAG elsewhere: no window exists, so the ledger is the whole chain and the tag resolver
    // runs against its current number, exactly as eth_getBlockByNumber does.
    auto const latest = co_await bcos::rpc::canonicalLatestNumber(*m_nodeService);
    auto [number, _] =
        bcos::rpc::getBlockNumberByTag(latest, blockTag, m_nodeService->safeBlockDepth(),
            m_nodeService->finalizedBlockDepth(), std::nullopt, std::nullopt, false);
    if (number < 0 || number > latest)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    try
    {
        co_return co_await bcos::rpc::canonicalBlockByNumber(*m_nodeService, number, blockFlag);
    }
    catch (bcos::Error const& e)
    {
        // The ledger reports a missing block either as a null block or by throwing; normalise
        // both to the same "not found" answer the callers already had.
        if (e.errorCode() == bcos::ledger::LedgerError::GetStorageError &&
            boost::get_error_info<bcos::Error::STDError>(e) == nullptr)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
        }
        throw;
    }
}

task::Task<void> DebugEndpoint::getRawHeader(const Json::Value& request, Json::Value& response)
{
    // params: blockNumberOrHash (QTY|TAG|DATA 32B)
    // result: the RLP-encoded Ethereum header (DATA); keccak256(result) is the block hash
    // eth_getBlockBy* reports, which is what kona-host checks.
    co_await requireEthereumLane(*m_nodeService->ledger(), "debug_getRawHeader");
    std::optional<crypto::HashType> requestedHash;
    auto const block =
        co_await rawBlockByParameter(request, "debug_getRawHeader", ledger::HEADER, &requestedHash);
    auto const headerPtr = block ? block->blockHeader() : nullptr;
    if (!headerPtr) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    auto const& header = *headerPtr;
    // The preimage-oracle contract is byte-exact: what this endpoint serves must be the bytes a
    // client hashes to obtain the block hash it asked for. encodeHeader is the whole gate --
    // it throws for a header with no Ethereum RLP form -- and the served bytes' own keccak is the
    // identity, so a hash request is bound to exactly that value.
    //
    // Do NOT validate through canonicalBlockHash here: on the eth lane it calls header.hash(),
    // which throws EmptyBlockHeaderHash for a header the ledger rebuilt from a row that never
    // stored a dataHash (observed on a real node). getRawBlock already works this way; keeping the
    // two endpoints on the same identity is what makes them agree byte for byte (review F4).
    bcos::bytes encoded;
    try
    {
        encoded = protocol::EthBlockHeader::encodeHeader(header);
    }
    catch (std::exception const& e)
    {
        // A header whose fields cannot be RLP-encoded (a native FISCO header with a
        // sub-second timestamp, or an incomplete fork-field set) has no Ethereum RLP to serve.
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InternalError, std::string("Header RLP encode failed: ") + e.what()));
    }
    if (requestedHash && bcos::crypto::keccak256Hash(bcos::ref(encoded)) != *requestedHash)
        [[unlikely]]
    {
        // The height this hash indexed now holds a different block; serving it would answer a
        // block the caller never asked for.
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    Json::Value result = toHexStringWithPrefix(encoded);
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> DebugEndpoint::getRawReceipts(const Json::Value& request, Json::Value& response)
{
    // params: blockNumberOrHash (QTY|TAG|DATA 32B)
    // result: [DATA] — one RLP-encoded receipt per transaction, in block order.
    //
    // kona-host reads the L1 receipts through this method to build the preimages its trace
    // needs; without it the challenger cannot produce an L1-side trace at all.
    co_await requireEthereumExecutorLane(*m_nodeService->ledger(), "debug_getRawReceipts");

    // TRANSACTIONS | RECEIPTS: the receipts-trie leaf is prefixed with the transaction's EIP-2718
    // type byte, which only the transaction carries, and the receipt itself comes from the same
    // block read. Both flags are required and they are disjoint (LedgerTypeDef.h): RECEIPTS is what
    // populates receipts(), so asking for TRANSACTIONS alone leaves receiptsSize() at 0 and the
    // count guard below then rejects every block that has a transaction. HEADER is added only so
    // the diagnostics below can name the block by its own number; nothing here reads the header.
    auto const block = co_await rawBlockByParameter(
        request, "debug_getRawReceipts", ledger::HEADER | ledger::TRANSACTIONS | ledger::RECEIPTS);
    if (!block) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    auto const blockNumber =
        block->blockHeader() ? block->blockHeader()->number() : protocol::BlockNumber{-1};

    Json::Value result(Json::arrayValue);
    // The receipts are zipped with the transactions by index, so a mismatch would silently
    // truncate or misalign the response. Fail closed before the loop rather than inside it.
    if (block->transactionsSize() != block->receiptsSize()) [[unlikely]]
    {
        WEB3_LOG(WARNING) << LOG_DESC("debug_getRawReceipts: block carries mismatched counts")
                          << LOG_KV("block", blockNumber)
                          << LOG_KV("transactions", block->transactionsSize())
                          << LOG_KV("receipts", block->receiptsSize());
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InternalError, "Block " + std::to_string(blockNumber) + " carries " +
                               std::to_string(block->transactionsSize()) + " transactions but " +
                               std::to_string(block->receiptsSize()) + " receipts"));
    }
    // Receipts come from the same read, indexed by the transaction's position, instead of one
    // getReceipt round trip per transaction (which also could not answer for a receipt row that
    // exists but is unreadable). The zip is what makes "block order" the only order this can emit.
    auto receipts = block->receipts();  // non-const: the holder view is not const-iterable
    std::size_t index = 0;
    for (auto anyTx : block->transactions())  // AnyTransaction::operator-> gives the Transaction
    {
        auto const& tx = *anyTx.operator->();
        auto const receipt = receipts.at(index);
        // Exactly the value the block's receipts trie holds (encodeReceiptLeaf): the same
        // encoding this node committed to, so a client can reassemble the trie from these
        // bytes and get the header's receiptsRoot back. The type byte comes from the signed
        // envelope through the engine helper the committed root uses, so the two cannot disagree.
        result.append(toHexStringWithPrefix(
            bcos::ledger::mpt::encodeReceiptLeaf(*receipt, bcos::engine::receiptLeafTypeByte(tx))));
        ++index;
    }
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> DebugEndpoint::getRawBlock(const Json::Value& request, Json::Value& response)
{
    // params: blockNumberOrHash (QTY|TAG|DATA 32B)
    // result: rlp([header, transactions, ommers, withdrawals?]) — geth's types.Block.
    //
    // op-succinct's witness generator (utils/host/src/fetcher.rs, get_l2_block_by_number) asks
    // for this with a block number and decodes the answer with OpBlock::decode, which is
    // rlp([header, txs, ommers]) for pre-Shanghai and the 4-element form once the header carries
    // a withdrawalsRoot. There is no fallback path, so a MethodNotFound here stops proving.
    co_await requireEthereumLane(*m_nodeService->ledger(), "debug_getRawBlock");

    // Read on the canonical plane, not the raw ledger, for two reasons the review called out:
    //   * HEADER is required, not optional -- getBlockData only populates block->blockHeader()
    //     when the HEADER flag is set (LedgerMethods.h), so TRANSACTIONS alone yields a null
    //     header and every block would answer "Block not found".
    //   * on the OP lane the backend holds the finalized chain only; the head op-node serves sits
    //     in the OpScheduler window, so rawBlockByParameter resolves the tag AND reads the block
    //     through canonicalBlockByNumber / canonicalBlockByHash (CanonicalReads.h).
    // A hash request is answered with the block that hash names, and the requested hash is handed
    // back so the identity re-check below binds the served bytes to it: if a canonical replacement
    // lands between the window lookup and the response, that check answers "not found" instead of
    // serving the replacement.
    std::optional<bcos::crypto::HashType> requestedHash;
    auto const block = co_await rawBlockByParameter(
        request, "debug_getRawBlock", ledger::HEADER | ledger::TRANSACTIONS, &requestedHash);
    if (!block) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    auto const headerPtr = block->blockHeader();
    if (!headerPtr) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    auto const& header = *headerPtr;
    auto const blockNumber = header.number();

    protocol::EthBlockData body;
    try
    {
        // The header codec is the single source of truth for field order; the constructor is
        // what converts a bcos header into the RLP domain (internal ms -> RLP seconds, and it
        // throws rather than silently truncating a sub-second timestamp).
        body.header = protocol::EthBlockHeader(header).data();
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InternalError,
            "Block " + std::to_string(blockNumber) + " has no Ethereum header: " + e.what()));
    }
    // The served bytes must be reproducible from the header's own fields, and encodeHeader is the
    // single gate for that: a native FISCO header (sub-second timestamp) or an OP header missing
    // fork fields has no Ethereum RLP form and throws here, which is exactly the set of blocks
    // this endpoint must refuse.
    //
    // Do NOT add canonicalBlockHash to this check. On the eth lane it calls header.hash(), which
    // throws EmptyBlockHeaderHash for a header the ledger rebuilt from a row that never stored a
    // dataHash -- observed on a real node, where it turned every debug_getRawBlock into -32603.
    // The identity that matters is keccak256(encodeHeader(header)): that is already an OP block's
    // identity, and on the eth lane those same bytes are what a client hashes to obtain the block
    // hash it asked for, so a hash request is validated against them below.
    bcos::bytes encodedHeader;
    try
    {
        encodedHeader = protocol::EthBlockHeader::encodeHeader(header);
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InternalError,
            "Block " + std::to_string(blockNumber) + " has no Ethereum header: " + e.what()));
    }
    if (requestedHash && bcos::crypto::keccak256Hash(bcos::ref(encodedHeader)) != *requestedHash)
        [[unlikely]]
    {
        // The height this hash indexed is now occupied by a different block. Answering the current
        // occupant would serve bytes for a block the caller never asked for.
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }

    // Transactions are the full SIGNED envelopes, i.e. exactly the values keyed in the
    // transactions trie, so a client can reassemble the trie from these bytes and recover
    // txsRoot. The stored row only keeps the signing payload (extraTransactionBytes) plus the
    // 65-byte r||s||yParity signature, so the envelope has to be spliced back together —
    // emitting the bare payload would produce a block whose txsRoot cannot be reproduced.
    body.transactions.reserve(block->transactionsSize());
    for (auto anyTx : block->transactions())  // AnyTransaction::operator-> gives the Transaction
    {
        auto const& tx = *anyTx.operator->();
        bcos::bytes envelope;
        try
        {
            envelope = fullRawEnvelope(tx);
        }
        catch (std::exception const& e)
        {
            // A transaction whose stored bytes cannot be turned back into its wire form cannot
            // enter the block RLP: a body with a hole in its transaction list is worse than no
            // answer, because its txsRoot would not match the header.
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InternalError, "Block " + std::to_string(blockNumber) +
                                                    " carries a transaction with no raw "
                                                    "envelope: " +
                                                    e.what()));
        }
        if (envelope.empty()) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InternalError, "Block " + std::to_string(blockNumber) +
                                                    " carries a transaction with no raw envelope"));
        }
        body.transactions.push_back(std::move(envelope));
    }

    // Withdrawals are fork-significant, and the header field means different things per fork and
    // per lane -- which is why the commitment re-check below must NOT compare the body's list
    // against it on the OP lane:
    //   * pre-Canyon (either lane): field absent -> 3-element block.
    //   * OP lane: the body is ALWAYS an empty list (an L2 never processes withdrawal credits),
    //     but the header field is the empty-trie root only for Canyon-Holocene. From Isthmus on it
    //     is the L2ToL1MessagePasser storage root -- a different commitment that has nothing to do
    //     with the body's (empty) withdrawals trie. See opstack-executor/OpEthBlockExecute.h
    //     ("withdrawalsRoot by fork") and OpEthCommitments.h. Comparing the empty list against it
    //     would reject every legitimate Isthmus+ block whose message-passer storage is non-empty.
    //     This endpoint cannot recompute that root from the body, so it serves the header's value
    //     verbatim and leaves verification to the payload path that owns the state.
    //   * eth lane: field present from Shanghai on and IS the withdrawals trie root, decoded from
    //     the sidecar row.
    if (!header.withdrawalsRoot().has_value())
    {
        body.withdrawals = std::nullopt;
    }
    else if (isOpStackLane(m_nodeService->executorVersion()))
    {
        body.withdrawals.emplace();
    }
    else
    {
        body.withdrawals.emplace();
        // The eth lane has no window: the sidecar row lives in the same flat state the ledger
        // exposes. (The OP lane returned above, and never writes or reads this row.)
        if (auto const stateStorage = m_nodeService->ledger()->getStateStorage())
        {
            auto const entry = co_await storage2::readOne(
                *stateStorage, executor_v1::StateKeyView{
                                   ledger::SYS_NUMBER_2_WITHDRAWALS, std::to_string(blockNumber)});
            if (!entry.has_value()) [[unlikely]]
            {
                // The header commits to a withdrawalsRoot but there is no sidecar row. That is
                // legitimate for exactly one block shape: a block whose withdrawals list is EMPTY
                // commits to the empty-trie root, and a build that never wrote a row for it (the
                // genesis block of this very chain) is telling the truth. Serve the empty list in
                // that case -- the commitments re-check below still proves it against the header.
                // Any other root means the row is genuinely missing (pruned, or written by an
                // incompatible build), and emitting an empty list would hand out a body that
                // contradicts its own header. Fail closed there, as before.
                if (bcos::ledger::mpt::calculateWithdrawalsRoot({}) != *header.withdrawalsRoot())
                {
                    BOOST_THROW_EXCEPTION(JsonRpcException(InternalError,
                        "Block " + std::to_string(blockNumber) +
                            " commits to a withdrawalsRoot but its withdrawals row is missing"));
                }
                WEB3_LOG(DEBUG) << LOG_DESC(
                                       "debug_getRawBlock: block has an empty withdrawals list")
                                << LOG_KV("number", blockNumber);
            }
            else
            {
                // The row holds the RLP LIST of per-item withdrawal encodings; decode each item
                // so the re-encode below goes through the same codec as the body.
                auto raw = bcos::bytes(entry->get().begin(), entry->get().end());
                auto items = bcos::ref(raw);
                auto const listHeader = bcos::codec::rlp::decodeHeader(items);
                if (!listHeader.isList || listHeader.payloadLength != items.size())
                {
                    BOOST_THROW_EXCEPTION(JsonRpcException(
                        InternalError, "Block " + std::to_string(blockNumber) +
                                           " carries a malformed withdrawals sidecar row"));
                }
                while (!items.empty())
                {
                    auto const itemStart = items;
                    auto const itemHeader = bcos::codec::rlp::decodeHeader(items);
                    auto const itemSize =
                        (itemStart.size() - items.size()) + itemHeader.payloadLength;
                    protocol::EthWithdrawal withdrawal;
                    withdrawal.rlpDecode(bcos::bytesConstRef(itemStart.data(), itemSize));
                    body.withdrawals->push_back(withdrawal.data());
                    items = items.getCroppedData(itemHeader.payloadLength);
                }
            }
        }
        else
        {
            // No flat state storage means the sidecar row cannot even be looked up. The header
            // still commits to a withdrawalsRoot, so guessing an empty list is not an option.
            BOOST_THROW_EXCEPTION(JsonRpcException(InternalError,
                "Block " + std::to_string(blockNumber) +
                    " commits to a withdrawalsRoot but this node cannot read the row"));
        }
    }

    // The body must reproduce the header's own commitments. This is the check that actually
    // proves the reconstruction is faithful -- a client rebuilds both tries from these bytes, so
    // a mismatch means the envelopes or the withdrawals were assembled wrong and the served block
    // could never be re-derived by anyone else. Cheap at these sizes (a handful of items), and it
    // is what turns a silently-wrong body into a loud error.
    auto const transactionsRoot = [&body] {
        std::vector<bcos::bytesConstRef> refs;
        refs.reserve(body.transactions.size());
        for (auto const& tx : body.transactions)
        {
            refs.emplace_back(tx.data(), tx.size());
        }
        return bcos::ledger::mpt::calculateTransactionsRoot(refs);
    }();
    if (transactionsRoot != header.txsRoot()) [[unlikely]]
    {
        // The two likely causes are a transaction whose signing preimage was not spliceable back
        // into its envelope, or a stored row whose payload does not match the committed trie.
        // Either way the bytes cannot be trusted, so refuse rather than hand out a body whose
        // transactionsRoot no client can recover from the header.
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InternalError, "Block " + std::to_string(blockNumber) +
                                                " body does not reproduce the header's "
                                                "transactionsRoot"));
    }
    // Withdrawals: the commitment re-check applies to the ETH lane only. There the header field is
    // the withdrawals trie root and the sidecar row is the list it commits to, so a mismatch means
    // the row was missing or mis-decoded and the body would contradict its own header.
    //
    // On the OP lane the field is NOT this list's root from Isthmus on (it is the
    // L2ToL1MessagePasser storage root -- OpEthBlockExecute.h "withdrawalsRoot by fork"), so
    // recomputing the empty list's root and comparing would reject every legitimate Isthmus+ block
    // whose message-passer storage is non-empty. The body's list is empty by construction and the
    // header value is served verbatim; verifying it needs the account state this endpoint does not
    // read. transactionsRoot is still checked on both lanes -- that commitment is the same on both.
    if (header.withdrawalsRoot().has_value() && !isOpStackLane(m_nodeService->executorVersion()))
    {
        if (body.withdrawals)
        {
            std::vector<bcos::bytes> encodedWithdrawals;
            encodedWithdrawals.reserve(body.withdrawals->size());
            for (auto const& withdrawal : *body.withdrawals)
            {
                bcos::bytes item;
                bcos::codec::rlp::encode(item, withdrawal);
                encodedWithdrawals.push_back(std::move(item));
            }
            std::vector<bcos::bytesConstRef> refs;
            refs.reserve(encodedWithdrawals.size());
            for (auto const& item : encodedWithdrawals)
            {
                refs.emplace_back(item.data(), item.size());
            }
            if (bcos::ledger::mpt::calculateWithdrawalsRoot(refs) != *header.withdrawalsRoot())
            {
                // The sidecar row was missing or did not decode to the committed list. Emitting an
                // empty list for a block whose header commits to a non-empty root is the one case
                // where being lenient produces a body that contradicts its own header.
                BOOST_THROW_EXCEPTION(JsonRpcException(
                    InternalError, "Block " + std::to_string(blockNumber) +
                                       " body does not reproduce the header's withdrawalsRoot"));
            }
        }
        else
        {
            // withdrawalsRoot present but the body has no list cannot happen here (the branch
            // above always emplaces one), kept explicit so the invariant is visible.
            BOOST_THROW_EXCEPTION(JsonRpcException(InternalError,
                "Block " + std::to_string(blockNumber) + " is missing its withdrawals list"));
        }
    }

    bcos::bytes encoded;
    try
    {
        protocol::EthBlock(std::move(body)).rlpEncode(encoded);
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InternalError, std::string("Block RLP encode failed: ") + e.what()));
    }
    Json::Value result = toHexStringWithPrefix(encoded);
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> DebugEndpoint::getRawTransaction(const Json::Value& request, Json::Value& response)
{
    // params: transaction hash (DATA 32B); result: the raw EIP-2718 envelope (DATA).
    //
    // The returned bytes are the transactions-trie value, so keccak(rlp(index)) -> these bytes
    // is what the header's txsRoot commits to. geth answers -32000 "not found" for a hash it
    // does not know; the same code keeps kona-host's retry semantics identical.
    co_await requireEthereumLane(*m_nodeService->ledger(), "debug_getRawTransaction");
    if (request.size() < 1 || !request[0U].isString())
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "debug_getRawTransaction expects one transaction hash"));
    }
    auto const hashText = toView(request[0U]);
    if (!isBlockHashString(hashText))
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid transaction hash"));
    }
    auto const hash = crypto::HashType(hashText, crypto::HashType::FromHex);
    auto const ledger = m_nodeService->ledger();

    protocol::Transaction::Ptr transaction;
    // On the OP lane a hash can resolve to a transaction in a block that a reorg has already
    // removed; the canonical reader is what tells the two apart, and it is the same source
    // eth_getTransactionByHash answers from. Elsewhere the ledger row is the answer.
    if (auto const& reader = m_nodeService->opCanonicalReader())
    {
        auto found = co_await reader->transactionOnHeadChain(hash, ledger::TRANSACTIONS_HASH);
        if (found)
        {
            transaction = std::move(found->transaction);
        }
    }
    else
    {
        auto hashList = std::make_shared<crypto::HashList>();
        hashList->push_back(hash);
        // A miss must be a value, not an exception: the ledger's batch read throws for a hash it
        // has no SYS_HASH_2_TX row for, and a throw escaping a co_await inside this task would be
        // rethrown by the dispatcher as an internal error (observed as -32603 on a real node).
        // Converting it here keeps geth's -32000 "not found", which kona-host keys retry logic on.
        transaction = co_await readTransactionOrNothing(*ledger, std::move(hashList));
    }
    if (!transaction) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Web3DefaultError, "not found"));
    }
    // Rebuild the full signed envelope: the stored row holds the signing payload plus the
    // 65-byte r||s||yParity signature, and only the spliced form hashes to the transaction's
    // canonical hash (keccak256 of these bytes), which is the key its trie leaf lives under.
    bcos::bytes envelope;
    try
    {
        envelope = fullRawEnvelope(*transaction);
    }
    catch (std::exception const& e)
    {
        // A transaction whose stored bytes cannot be turned back into its wire form has no
        // bytes answer that could hash into an Ethereum transactions trie.
        BOOST_THROW_EXCEPTION(JsonRpcException(
            Web3DefaultError, std::string("transaction has no raw envelope: ") + e.what()));
    }
    if (envelope.empty()) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Web3DefaultError, "transaction has no raw envelope"));
    }
    Json::Value result = toHexStringWithPrefix(envelope);
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
