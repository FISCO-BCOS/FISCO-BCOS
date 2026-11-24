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
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/util.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/RpcChainPolicy.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
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

task::Task<protocol::BlockNumber> DebugEndpoint::resolveRawBlockParameter(
    const Json::Value& request, std::string_view method)
{
    // One block parameter, shared by debug_getRawHeader and debug_getRawReceipts: a block
    // number, a tag, or a 32-byte block hash (which kona-host uses for preimages).
    auto const ledger = m_nodeService->ledger();
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

    protocol::BlockNumber blockNumber = 0;
    protocol::BlockNumber head = 0;
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
        auto [number, _] =
            bcos::rpc::getBlockNumberByTag(latest, blockTag, m_nodeService->safeBlockDepth(),
                m_nodeService->finalizedBlockDepth(), std::nullopt, std::nullopt, false);
        blockNumber = number;
        head = latest;
    }
    if (blockNumber < 0 || blockNumber > head)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    co_return blockNumber;
}

task::Task<void> DebugEndpoint::getRawHeader(const Json::Value& request, Json::Value& response)
{
    // params: blockNumberOrHash (QTY|TAG|DATA 32B)
    // result: the RLP-encoded Ethereum header (DATA); keccak256(result) is the block hash
    // eth_getBlockBy* reports (canonicalBlockHash), which is what kona-host checks.
    co_await requireEthereumLane(*m_nodeService->ledger(), "debug_getRawHeader");
    auto const ledger = m_nodeService->ledger();
    auto const blockNumber = co_await resolveRawBlockParameter(request, "debug_getRawHeader");
    auto const block = co_await ledger::getBlockData(*ledger, blockNumber, ledger::HEADER);
    auto const headerPtr = block ? block->blockHeader() : nullptr;
    if (!headerPtr) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }
    auto const& header = *headerPtr;
    // The preimage-oracle contract is byte-exact: the served RLP must hash back to the block's
    // published hash (canonicalBlockHash). Rather than re-spell the lane discriminator (which
    // over-refuses pre-Canyon OP headers and under-checks Eth-lane headers), verify the exact
    // invariant this endpoint needs and consult the owning module: serve iff the canonical hash
    // equals keccak256 of the encoded header. This closes both directions at once.
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
    if (protocol::canonicalBlockHash(header) != bcos::crypto::keccak256Hash(bcos::ref(encoded)))
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InternalError, "Block " + std::to_string(blockNumber) + " has no Ethereum header"));
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
    auto const ledger = m_nodeService->ledger();
    auto const blockNumber = co_await resolveRawBlockParameter(request, "debug_getRawReceipts");

    // TRANSACTIONS | RECEIPTS: the receipts-trie leaf is prefixed with the transaction's EIP-2718
    // type byte, which only the transaction carries, and the receipt itself comes from the same
    // block read. Both flags are required and they are disjoint (LedgerTypeDef.h): RECEIPTS is what
    // populates receipts(), so asking for TRANSACTIONS alone leaves receiptsSize() at 0 and the
    // count guard below then rejects every block that has a transaction.
    auto const block = co_await ledger::getBlockData(
        *ledger, blockNumber, ledger::TRANSACTIONS | ledger::RECEIPTS);
    if (!block) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Block not found"));
    }

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
