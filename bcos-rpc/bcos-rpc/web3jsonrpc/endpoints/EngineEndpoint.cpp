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
 * @file EngineEndpoint.cpp
 * @author: GitHub Copilot
 * @date 2026/5/7
 */

#include "EngineEndpoint.h"
#include "include/BuildInfo.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-crypto/kzg/Kzg4844.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/LegacyStorageMethods.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-rlp-protocol/EthWithdrawal.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Error.h>
#include <algorithm>
#include <exception>
#include <string_view>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
/// Clears the OP newPayload in-flight flag, including on exception unwind.
struct OpPayloadBusyReset
{
    std::atomic<bool>& flag;
    bool owned;
    OpPayloadBusyReset(std::atomic<bool>& busy, bool owns) : flag(busy), owned(owns) {}
    OpPayloadBusyReset(OpPayloadBusyReset const&) = delete;
    OpPayloadBusyReset& operator=(OpPayloadBusyReset const&) = delete;
    OpPayloadBusyReset(OpPayloadBusyReset&&) = delete;
    OpPayloadBusyReset& operator=(OpPayloadBusyReset&&) = delete;
    ~OpPayloadBusyReset()
    {
        if (owned)
        {
            flag.store(false, std::memory_order_release);
        }
    }
};

/// Map unexpected service errors to a SHORT -32603: the reason is echoed so the operator can
/// diagnose it, but boost's file/line diagnostics are not. EngineRpcTest pins both halves
/// (isShortInternalError), so this is a deliberate contract, not an accident.
[[noreturn]] void rethrowAsEngineInternalError(std::string_view reason)
{
    std::string message = "Internal error";
    if (!reason.empty())
    {
        message += ": ";
        message += reason;
    }
    BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, std::move(message)));
}

[[noreturn]] void rethrowAsEngineInternalError(std::exception const& e)
{
    rethrowAsEngineInternalError(std::string_view(e.what()));
}

/// bcos::Error carries its reason in ErrorMessage, not in what() (Exception::what() returns
/// only errinfo_comment, which BCOS_ERROR never sets) — a catch(std::exception) arm would
/// collapse every BCOS_ERROR from the scheduler/ledger to a bare "Internal error". Catch it
/// explicitly so the reason survives into the -32603.
[[noreturn]] void rethrowAsEngineInternalError(bcos::Error const& e)
{
    rethrowAsEngineInternalError(std::string_view(e.errorMessage()));
}

/// engine_getPayloadBodiesByRangeV1 request ceiling: the spec only fixes a floor
/// (a client MUST serve count >= 32); geth's getBodiesByRange rejects count > 1024 with
/// -38004 Too large request, and so does this endpoint. ByHash carries no cap, matching
/// geth (the listener's HTTP body-size limit bounds the request instead).
constexpr uint64_t c_maxPayloadBodiesRange = 1024;

/// Strict uint64 hex-quantity reader for engine params, mirroring EngineHelper.cpp's
/// translation-unit-local parseQuantity: jsoncpp's asString() stringifies numbers/bools
/// (so an isString() gate is what makes the check complete) and a bare fromQuantity reads
/// non-prefixed input as hex anyway — a malformed quantity from the CL must be -32602,
/// never the -32603 the RPC entry point would funnel a std::invalid_argument into.
uint64_t parseEngineQuantity(Json::Value const& value, std::string_view field)
{
    if (value.isString())
    {
        if (auto parsed = bcos::safeFromQuantity(value.asString()))
        {
            return *parsed;
        }
    }
    BOOST_THROW_EXCEPTION(JsonRpcException(
        InvalidParams, std::string(field) + " must be a uint64 hex quantity string"));
}

/// Decode a SYS_NUMBER_2_WITHDRAWALS row (the RLP LIST of the block's per-item
/// withdrawal RLP, written by EthereumBlockVerifier's commit) into the WithdrawalV1
/// JSON array. A malformed row is node-local data corruption: -32603, not a null body.
Json::Value decodeWithdrawalsJson(bcos::bytes raw)
{
    Json::Value withdrawals(Json::arrayValue);
    auto items = bcos::ref(raw);
    auto const listHeader = bcos::codec::rlp::decodeHeader(items);
    if (!listHeader.isList || listHeader.payloadLength != items.size())
    {
        rethrowAsEngineInternalError("malformed withdrawals sidecar row (not a closed RLP list)");
    }
    while (!items.empty())
    {
        auto const itemStart = items;
        auto const itemHeader = bcos::codec::rlp::decodeHeader(items);
        auto const itemSize = (itemStart.size() - items.size()) + itemHeader.payloadLength;
        bcos::protocol::EthWithdrawal withdrawal;
        withdrawal.rlpDecode(bcos::bytesConstRef(itemStart.data(), itemSize));
        auto const& data = withdrawal.data();
        Json::Value item(Json::objectValue);
        item["index"] = toQuantity(data.index);
        item["validatorIndex"] = toQuantity(data.validatorIndex);
        item["address"] = data.address.hexPrefixed();
        item["amount"] = toQuantity(data.amount);
        withdrawals.append(std::move(item));
        items = items.getCroppedData(itemHeader.payloadLength);
    }
    return withdrawals;
}
}  // namespace

EngineEndpoint::EngineEndpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService))
{}

void EngineEndpoint::buildEngineNotAvailableError(Json::Value& response) const
{
    Json::Value error;
    error["code"] = Web3DefaultError;
    error["message"] = "Engine service is not available on this node";
    response["jsonrpc"] = "2.0";
    response["error"] = std::move(error);
}

task::Task<void> EngineEndpoint::exchangeCapabilities(
    const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        // The engine service may be absent (e.g. a MAX/tars node without
        // op_engine_rpc wiring). Return a clean JSON-RPC error instead of
        // dereferencing null under release builds (where assert is compiled out).
        buildEngineNotAvailableError(response);
        co_return;
    }

    std::vector<std::string> remoteCaps;
    auto const& capsArray = request[0u];
    for (auto const& cap : capsArray)
    {
        // This is the FIRST Engine method a CL calls, and asString() throws
        // Json::LogicError on an array/object element — which the RPC entry point turns
        // into -32603 carrying boost's diagnostic string back to the caller.
        if (!cap.isString())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                InvalidParams, "engine_exchangeCapabilities expects an array of method names"));
        }
        remoteCaps.push_back(cap.asString());
    }

    auto caps = co_await engineService->exchangeCapabilities(std::move(remoteCaps));
    Json::Value result(Json::arrayValue);
    for (auto const& cap : caps)
    {
        result.append(cap);
    }
    buildJsonContent(result, response);
}

void EngineEndpoint::buildUnimplementedVersionError(
    std::string_view method, Json::Value& response) const
{
    // -38005: this method version is not implemented. The dispatcher stamps JSON-RPC id.
    Json::Value error(Json::objectValue);
    error["code"] = EngineError::UnsupportedFork;
    error["message"] = std::string(method) + " is not yet supported";
    response["jsonrpc"] = "2.0";
    response["error"] = std::move(error);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV1(
    const Json::Value& request, Json::Value& response)
{
    co_await handleForkchoiceUpdated(engine::ApiVersion::V1, request, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV2(
    const Json::Value& request, Json::Value& response)
{
    co_await handleForkchoiceUpdated(engine::ApiVersion::V2, request, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV3(
    const Json::Value& request, Json::Value& response)
{
    co_await handleForkchoiceUpdated(engine::ApiVersion::V3, request, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV4(const Json::Value&, Json::Value& response)
{
    // Prague forkchoiceUpdated shape; not implemented (Karst builds payloads on V3).
    buildUnimplementedVersionError("engine_forkchoiceUpdatedV4", response);
    co_return;
}

task::Task<void> EngineEndpoint::handleForkchoiceUpdated(
    engine::ApiVersion version, const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(response);
        co_return;
    }

    auto forkchoiceState = parseForkchoiceState(request);
    auto payloadAttrs = parsePayloadAttributes(request, version);
    // Map typed engine failures to spec codes; other exceptions stay -32603.

    engine::ForkchoiceUpdatedResult engineResult;
    try
    {
        engineResult = co_await engineService->updateForkchoice(forkchoiceState,
            payloadAttrs.has_value() ? &*payloadAttrs : nullptr, static_cast<uint32_t>(version));
    }
    catch (engine::InvalidPayloadAttributes const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::InvalidPayloadAttributes,
            std::string("Invalid payload attributes: ") + e.what()));
    }
    catch (engine::UnsupportedFork const& e)
    {
        // Keep -38005 and preserve the service error text.
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (engine::UnsupportedEngineApiVersion const& e)
    {
        // Method-version mismatch maps to -38005.
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (engine::InvalidForkchoiceState const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::InvalidForkchoiceState,
            std::string("Invalid forkchoice state: ") + e.what()));
    }
    catch (bcos::Error const& e)
    {
        rethrowAsEngineInternalError(e);
    }
    catch (std::exception const& e)
    {
        rethrowAsEngineInternalError(e);
    }
    auto jsonResult = combineForkchoiceUpdatedResult(engineResult, version);
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::getPayloadV1(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V1, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV2(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V2, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV3(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V3, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV4(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V4, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV5(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V5, request, response);
}

task::Task<void> EngineEndpoint::handleGetPayload(
    engine::ApiVersion version, const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(response);
        co_return;
    }

    if (request.size() < 1 || !request[0u].isString())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "engine_getPayload expects [payloadId]"));
    }
    engine::PayloadID payloadId = request[0u].asString();
    engine::GetPayloadResult engineResult;
    try
    {
        engineResult =
            co_await engineService->getPayload(payloadId, static_cast<uint32_t>(version));
    }
    catch (engine::UnknownPayload const&)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::UnknownPayload,
            "Unknown payload: no build process identified by the given payloadId"));
    }
    catch (engine::IncompatiblePayloadVersion const&)
    {
        // Payload was built by a different Engine API method version.
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::UnsupportedFork,
            "Unsupported fork: payload was built by a different method version"));
    }
    catch (engine::UnsupportedFork const& e)
    {
        // The payload's fork is outside this method's window (getPayloadV4 for a Karst payload,
        // V5 for a pre-Karst one).
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (engine::UnsupportedEngineApiVersion const& e)
    {
        // Method-version mismatch is a -38005, same class as an unsupported fork.
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (bcos::Error const& e)
    {
        rethrowAsEngineInternalError(e);
    }
    catch (std::exception const& e)
    {
        rethrowAsEngineInternalError(e);
    }
    if (!engineResult)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::UnknownPayload,
            "Unknown payload: no build process identified by the given payloadId"));
    }

    Json::Value result;
    combineGetPayloadResponse(result, engineResult, version);
    buildJsonContent(result, response);
}

task::Task<void> EngineEndpoint::newPayloadV1(const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V1, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV2(const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V2, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV3(const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V3, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV4(const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V4, request, response);
}

task::Task<void> EngineEndpoint::handleNewPayload(
    engine::ApiVersion version, const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(response);
        co_return;
    }

    // Parse before taking the latch so a malformed request still answers InvalidParams
    // (-32602) while a V4 payload is in flight; the latch below only bounds execution.
    auto newPayloadReq = parseNewPayloadRequest(request, version);

    // One in-flight V4 newPayload; a second concurrent call answers SYNCING.
    const bool opExecution = version == engine::ApiVersion::V4;
    if (opExecution && m_opPayloadBusy.exchange(true, std::memory_order_acq_rel))
    {
        auto syncingStatus = serializePayloadStatus(
            engine::PayloadStatus{
                .latestValidHash = std::nullopt,
                .validationError = std::nullopt,
                .status = engine::PayloadValidationStatus::Syncing,
            },
            version);
        buildJsonContent(syncingStatus, response);
        co_return;
    }
    OpPayloadBusyReset busyReset{m_opPayloadBusy, opExecution};

    engine::PayloadStatus engineResult;
    try
    {
        engineResult =
            co_await engineService->newPayload(newPayloadReq, static_cast<uint32_t>(version));
    }
    catch (engine::UnsupportedFork const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (engine::UnsupportedEngineApiVersion const& e)
    {
        // Method-version mismatch is a -38005, same class as an unsupported fork.
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (engine::InvalidPayloadAttributes const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::InvalidPayloadAttributes,
            std::string("Invalid payload attributes: ") + e.what()));
    }
    catch (bcos::Error const& e)
    {
        rethrowAsEngineInternalError(e);
    }
    catch (std::exception const& e)
    {
        rethrowAsEngineInternalError(e);
    }
    auto result = serializePayloadStatus(engineResult, version);
    buildJsonContent(result, response);
}

task::Task<Json::Value> EngineEndpoint::payloadBodyAtNumber(protocol::BlockNumber number)
{
    auto const& ledger = m_nodeService->ledger();
    protocol::Block::Ptr block;
    try
    {
        block = co_await ledger::getBlockData(
            *ledger, number, bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS);
    }
    catch (std::exception const&)
    {
        // An unavailable (pruned / never-seen) block is a null body, matching geth's
        // GetBlockByNumber-miss path — not an RPC error.
        co_return Json::nullValue;
    }

    Json::Value body(Json::objectValue);
    Json::Value transactions(Json::arrayValue);
    for (auto const& tx : block->transactions())
    {
        // A committed EL block's transactions are all Web3 (EIP-2718): the signing payload
        // (extraTransactionBytes) plus the r||s||yParity signature reassembles into the exact
        // wire bytes the block was submitted with. Anything else here is node-local data
        // corruption — fail loudly (-32603) rather than emitting a body the CL would
        // hash-check into a confusing INVALID downstream.
        if (tx->type() != static_cast<uint8_t>(protocol::TransactionType::Web3Transaction))
        {
            rethrowAsEngineInternalError(
                "block carries a transaction without an EIP-2718 wire form");
        }
        try
        {
            transactions.append(
                toHexStringWithPrefix(bcostars::protocol::reassembleWeb3RawTransaction(
                    tx->extraTransactionBytes(), tx->signatureData())));
        }
        catch (std::exception const& e)
        {
            rethrowAsEngineInternalError(e);
        }
    }
    body["transactions"] = std::move(transactions);

    // withdrawals: the spec mandates JSON null for pre-Shanghai bodies; for Shanghai+
    // (header carries withdrawalsRoot) the list comes from the SYS_NUMBER_2_WITHDRAWALS
    // sidecar row the EL verifier's commit writes. A Shanghai+ block without the row
    // (committed before the row existed, or a ledger whose raw state storage is not
    // readable through this interface) is an unavailable body -> null.
    if (block->blockHeader()->withdrawalsRoot().has_value())
    {
        auto const stateStorage = ledger->getStateStorage();
        if (!stateStorage)
        {
            co_return Json::nullValue;
        }
        auto const entry = co_await storage2::readOne(*stateStorage,
            executor_v1::StateKeyView{ledger::SYS_NUMBER_2_WITHDRAWALS, std::to_string(number)});
        if (!entry.has_value())
        {
            co_return Json::nullValue;
        }
        auto const rawWithdrawals = entry->get();
        body["withdrawals"] = decodeWithdrawalsJson(
            bcos::bytes(rawWithdrawals.begin(), rawWithdrawals.end()));
    }
    else
    {
        body["withdrawals"] = Json::nullValue;
    }
    co_return body;
}

task::Task<void> EngineEndpoint::getPayloadBodiesByHashV1(
    const Json::Value& request, Json::Value& response)
{
    auto const& hashes = request[0u];
    if (!hashes.isArray())
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "engine_getPayloadBodiesByHashV1 expects an array of block hashes"));
    }
    auto const& ledger = m_nodeService->ledger();
    Json::Value result(Json::arrayValue);
    for (auto const& hashValue : hashes)
    {
        // parseH256 maps malformed hex / wrong length to -32602.
        auto const blockHash = parseH256(
            hashValue.isString() ? std::string_view(hashValue.asString()) : std::string_view());
        protocol::BlockNumber number = -1;
        try
        {
            number = co_await ledger::getBlockNumber(*ledger, crypto::HashType(blockHash));
        }
        catch (std::exception const&)
        {
            // A lookup fault on an unknown hash reads as "not found" below.
        }
        if (number < 0)
        {
            result.append(Json::nullValue);
            continue;
        }
        result.append(co_await payloadBodyAtNumber(number));
    }
    buildJsonContent(result, response);
}

task::Task<void> EngineEndpoint::getPayloadBodiesByRangeV1(
    const Json::Value& request, Json::Value& response)
{
    if (request.size() < 2)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "engine_getPayloadBodiesByRangeV1 expects [start, count]"));
    }
    auto const start = parseEngineQuantity(request[0u], "start");
    auto const count = parseEngineQuantity(request[1u], "count");
    // Spec: start or count below 1 is -32602; count above the ceiling is -38004.
    if (start < 1 || count < 1)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "engine_getPayloadBodiesByRangeV1: start and count must be >= 1"));
    }
    if (count > c_maxPayloadBodiesRange)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::TooLargeRequest,
            "engine_getPayloadBodiesByRangeV1: requested count too large: " +
                std::to_string(count)));
    }
    auto const& ledger = m_nodeService->ledger();
    auto const head = co_await ledger::getCurrentBlockNumber(*ledger);
    Json::Value result(Json::arrayValue);
    // A range beyond the latest block answers an empty array (no trailing nulls); the
    // start <= head guard also keeps start + count - 1 from overflowing.
    if (head >= 0 && start <= static_cast<uint64_t>(head))
    {
        auto const last = std::min(start + count - 1, static_cast<uint64_t>(head));
        for (auto number = start; number <= last; ++number)
        {
            result.append(co_await payloadBodyAtNumber(static_cast<protocol::BlockNumber>(number)));
        }
    }
    buildJsonContent(result, response);
}

namespace
{
/// engine_getBlobsV1 request ceiling: the spec mandates support for at least 128 versioned
/// hashes and a -38004 above the client's cap.
constexpr std::size_t c_maxGetBlobsRequest = 128;
/// Ledger fallback scan depth: the pool is the spec's data source; the committed-block
/// blob sidecar rows are only consulted for the recent window a CL realistically asks
/// about (its own head's payloads). Each step is one storage read.
constexpr protocol::BlockNumber c_getBlobsLedgerScanDepth = 128;
}  // namespace

task::Task<void> EngineEndpoint::getBlobsV1(const Json::Value& request, Json::Value& response)
{
    auto const& hashes = request[0u];
    if (!hashes.isArray())
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "engine_getBlobsV1 expects an array of blob versioned hashes"));
    }
    if (hashes.size() > c_maxGetBlobsRequest)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::TooLargeRequest,
            "engine_getBlobsV1: requested count too large: " + std::to_string(hashes.size())));
    }
    std::vector<crypto::HashType> versionedHashes;
    versionedHashes.reserve(hashes.size());
    for (auto const& hashValue : hashes)
    {
        // parseH256 maps malformed hex / wrong length to -32602.
        versionedHashes.emplace_back(crypto::HashType(
            parseH256(hashValue.isString() ? std::string_view(hashValue.asString()) :
                                             std::string_view())));
    }

    // Pool first (the spec's data source: "fetch blobs from the execution layer blob
    // pool"), then the committed blocks' blob sidecar rows for the recent window.
    std::vector<std::optional<engine::BlobItem>> items(versionedHashes.size());
    if (auto* memPool = m_nodeService->memPool(); memPool != nullptr)
    {
        items = memPool->blobsByVersionedHashes(versionedHashes);
    }
    auto missing = static_cast<std::size_t>(
        std::count(items.begin(), items.end(), std::nullopt));
    if (missing > 0)
    {
        auto const& ledger = m_nodeService->ledger();
        auto const stateStorage = ledger ? ledger->getStateStorage() : nullptr;
        if (stateStorage)
        {
            auto const head = co_await ledger::getCurrentBlockNumber(*ledger);
            for (auto number = head; number >= 0 && missing > 0 &&
                 head - number < c_getBlobsLedgerScanDepth;
                 --number)
            {
                auto const entry = co_await storage2::readOne(*stateStorage,
                    executor_v1::StateKeyView{
                        ledger::SYS_NUMBER_2_BLOBS, std::to_string(number)});
                if (!entry.has_value())
                {
                    continue;
                }
                auto const rawRow = entry->get();
                auto rowBytes = bcos::bytes(rawRow.begin(), rawRow.end());
                auto in = bcos::ref(rowBytes);
                try
                {
                    auto const outerHead = codec::rlp::decodeHeader(in);
                    bcos::byte* const outerStart = in.data();
                    while (static_cast<std::size_t>(in.data() - outerStart) <
                           outerHead.payloadLength)
                    {
                        auto const itemHead = codec::rlp::decodeHeader(in);
                        bcos::byte* const itemStart = in.data();
                        bcos::bytes commitment, proof, blob;
                        codec::rlp::decodeItems(in, commitment, proof, blob);
                        if (static_cast<std::size_t>(in.data() - itemStart) !=
                                itemHead.payloadLength ||
                            commitment.size() != crypto::kzg::CommitmentSize)
                        {
                            break;  // corrupt row: stop mining it, other rows may still match
                        }
                        auto const versionedHash =
                            crypto::kzg::versionedHashFromCommitment(bcos::ref(commitment));
                        for (std::size_t j = 0; j < versionedHashes.size(); ++j)
                        {
                            if (!items[j].has_value() && versionedHashes[j] == versionedHash)
                            {
                                items[j] = engine::BlobItem{.commitment = std::move(commitment),
                                    .proof = std::move(proof),
                                    .blob = std::move(blob)};
                                --missing;
                                break;
                            }
                        }
                    }
                }
                catch (std::exception const&)
                {
                    // A corrupt sidecar row is not an RPC error: the hashes it would have
                    // served read as missing (null), the same as never-committed blobs.
                    continue;
                }
            }
        }
    }

    Json::Value result(Json::arrayValue);
    for (auto const& item : items)
    {
        if (!item.has_value())
        {
            result.append(Json::nullValue);
            continue;
        }
        Json::Value blobAndProof(Json::objectValue);
        blobAndProof["blob"] = toHexStringWithPrefix(bcos::ref(item->blob));
        blobAndProof["proof"] = toHexStringWithPrefix(bcos::ref(item->proof));
        result.append(std::move(blobAndProof));
    }
    buildJsonContent(result, response);
}

task::Task<void> EngineEndpoint::getClientVersionV1(
    const Json::Value& request, Json::Value& response)
{
    // Static self-report (execution-apis identification.md): the CL's own ClientVersionV1
    // (params[0]) is informational only — accepted when object-shaped, never consulted.
    if (request.size() >= 1 && !request[0u].isObject())
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "engine_getClientVersionV1 expects [clientVersion]"));
    }
    // "FB" is unreserved in the spec's ClientCode list (execution-apis identification.md
    // invites unlisted clients to pick a non-colliding two-letter code).
    Json::Value self(Json::objectValue);
    self["code"] = "FB";
    self["name"] = "FISCO-BCOS";
    self["version"] = std::string("v") + FISCO_BCOS_PROJECT_VERSION;
    // commit is DATA, 4 bytes — the first four bytes of the build's commit hash.
    self["commit"] =
        "0x" + std::string(FISCO_BCOS_COMMIT_HASH).substr(0, 8);
    Json::Value result(Json::arrayValue);
    result.append(std::move(self));
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> EngineEndpoint::exchangeClientVersionV1(
    const Json::Value& request, Json::Value& response)
{
    // Pre-rename draft name of engine_getClientVersionV1; older CLs still call it.
    co_await getClientVersionV1(request, response);
}
