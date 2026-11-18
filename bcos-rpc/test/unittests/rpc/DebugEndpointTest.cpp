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
 * @file DebugEndpointTest.cpp
 * @brief debug_dbGet / debug_getRawHeader JSON-RPC endpoint tests (kona-host fault-proof
 *        preimage server support)
 */

#include "../common/RPCFixture.h"
#include <bcos-codec/abi/RevertReason.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <magic_enum/magic_enum.hpp>
#include <map>
#include <string>

namespace bcos::test
{
namespace
{
/// Build a base-class header (BlockHeaderImpl) with every Eth-LONDON field populated, the
/// same shape the EthBlockHeader bridge expects. The RLP hash is injected by the caller via
/// EthBlockHeader::calculateRLPHash, mirroring EthBlockHeaderTest.cpp's makeEthHeader.
bcos::protocol::BlockHeader::Ptr makeEthHeader()
{
    auto tars = std::make_shared<bcostars::BlockHeader>();
    auto& data = tars->data;
    // RPCFixture's FakeLedger seeds 20 blocks (numbers 0..19); this header becomes block 20 so
    // injectBlock's hash->number mapping lands on the vector slot getBlockData(number) reads.
    data.blockNumber = 20;
    data.timestamp = 1700000000 * 1000LL;  // BlockHeader stores ms; the bridge converts to s
    data.gasLimit = "30000000";
    data.gasUsed = "21000";
    data.coinbase.assign(20, static_cast<char>(0xab));
    data.uncleHash.assign(32, static_cast<char>(0xee));
    data.stateRoot.assign(32, static_cast<char>(0x11));
    data.txsRoot.assign(32, static_cast<char>(0x22));
    data.receiptRoot.assign(32, static_cast<char>(0x33));
    data.prevRandao.assign(32, static_cast<char>(0x44));
    data.logsBloom.assign(256, static_cast<char>(0xcd));
    data.difficulty = "0";
    data.nonce.assign(8, static_cast<char>(0x00));
    bcostars::ParentInfo parentInfo;
    parentInfo.blockNumber = 19;
    parentInfo.blockHash.assign(32, static_cast<char>(0x55));
    data.parentInfo.push_back(parentInfo);
    data.baseFee = "1000000000";

    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>(tars);
    header->setEthBlockVersion(bcos::protocol::EthBlockVersion::LONDON);
    return header;
}
}  // namespace

class DebugEndpointFixture : public RPCFixture
{
public:
    using MPTNodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

    DebugEndpointFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_TEST(web3JsonRpc != nullptr);
        // The debug namespace is OP-lane-only (requireOpStackLane reads the executor_version
        // SYS_CONFIG entry); the challenger data plane (kona-host) only talks to an OP node.
        m_ledger->setSystemConfig(
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(bcos::ledger::OPSTACK_EXECUTOR_VERSION));
    }

    /// Inject the type-erased MPT node reader; the AnyStorage view is non-owning, m_mptNodes
    /// (a fixture member) plays the Initializer's ownership role and outlives the NodeService.
    void wireReader()
    {
        nodeService->setMPTNodeReader(
            std::make_shared<bcos::storage2::AnyStorage<bcos::h256, bcos::bytes>>(m_mptNodes));
    }

    Json::Value call(std::string const& method, Json::Value params)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = std::move(method);
        req["params"] = std::move(params);
        return request(printJson(req));
    }

    Json::Value request(std::string const& req)
    {
        Json::Value value;
        Json::Reader reader;
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(req, [&promise](bcos::bytes resp, boost::beast::http::status) {
            promise.set_value(std::move(resp));
        });
        auto jsonBytes = promise.get_future().get();
        std::string_view json((char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
    MPTNodeStorage m_mptNodes;
};

BOOST_FIXTURE_TEST_SUITE(DebugEndpointTest, DebugEndpointFixture)

BOOST_AUTO_TEST_CASE(RevertReasonDynamicOffsetBounds)
{
    // Error("bad") with an in-bounds 64-byte offset (geth ABI decoding permits padding).
    bytes output(103);
    output[0] = 0x08;
    output[1] = 0xc3;
    output[2] = 0x79;
    output[3] = 0xa0;
    output[35] = 64;
    output[99] = 3;
    output[100] = 'b';
    output[101] = 'a';
    output[102] = 'd';
    auto reason = codec::abi::decodeRevertReason(ref(output));
    BOOST_REQUIRE(reason.has_value());
    BOOST_CHECK_EQUAL(*reason, "bad");
    output[35] = 255;
    BOOST_CHECK(!codec::abi::decodeRevertReason(ref(output)));
    output[35] = 64;
    output[99] = 4;
    BOOST_CHECK(!codec::abi::decodeRevertReason(ref(output)));
}

BOOST_AUTO_TEST_CASE(PanicCodeIsCheckedBeforeNarrowing)
{
    bytes output(36);
    output[0] = 0x4e;
    output[1] = 0x48;
    output[2] = 0x7b;
    output[3] = 0x71;
    output[27] = 1;  // 2^64 + 1 must not decode as assert(false).
    output[35] = 1;
    auto reason = codec::abi::decodeRevertReason(ref(output));
    BOOST_REQUIRE(reason);
    BOOST_CHECK_EQUAL(*reason, "unknown panic code: 0x10000000000000001");
}

BOOST_AUTO_TEST_CASE(TraceMethodHasAnIndependentGate)
{
    EndpointsMapping const defaultMapping;
    EndpointsMapping const preimages(false, false, true);
    EndpointsMapping const trace(false, false, false, true);
    BOOST_CHECK(!defaultMapping.findHandler("debug_traceTransaction"));
    BOOST_CHECK(!preimages.findHandler("debug_traceTransaction"));
    BOOST_CHECK(trace.findHandler("debug_traceTransaction").has_value());
    BOOST_CHECK(!trace.findHandler("debug_dbGet"));
    BOOST_CHECK(!trace.findHandler("debug_getRawHeader"));
    BOOST_CHECK(!trace.findHandler("debug_getRawReceipts"));
    BOOST_CHECK(preimages.findHandler("debug_getRawReceipts").has_value());
}

BOOST_AUTO_TEST_CASE(TraceOptionsAndCallFrameWireContract)
{
    class TraceScheduler : public FakeScheduler2
    {
    public:
        using FakeScheduler2::FakeScheduler2;
        void traceTransaction(crypto::HashType const& hash, scheduler::TraceOptions options,
            std::function<void(Error::Ptr, std::shared_ptr<scheduler::CallTrace>)> callback)
            override
        {
            observedHash = hash;
            observedOptions = options;
            ++invocations;
            callback(error, result);
        }
        crypto::HashType observedHash;
        scheduler::TraceOptions observedOptions;
        int invocations = 0;
        Error::Ptr error;
        std::shared_ptr<scheduler::CallTrace> result = std::make_shared<scheduler::CallTrace>();
    };
    auto tracing = std::make_shared<TraceScheduler>(m_ledger, m_blockFactory);
    auto service = std::make_shared<bcos::rpc::NodeService>(
        m_ledger, tracing, txPool, nullptr, nullptr, m_blockFactory, nullptr);
    DebugEndpoint endpoint(service);
    auto& frame = *tracing->result;
    frame.type = "CALL";
    frame.from = "0x0000000000000000000000000000000000000001";
    frame.to = "0x0000000000000000000000000000000000000002";
    frame.value = "0x0";
    frame.gas = "0x5208";
    frame.gasUsed = "0x5208";
    frame.input = "0x";
    auto child = std::make_unique<scheduler::CallTrace>();
    child->from = frame.from;
    child->to = frame.to;
    child->gas = frame.gas;
    child->gasUsed = frame.gasUsed;
    child->input = frame.input;
    child->type = "STATICCALL";
    child->value.clear();
    child->error = "execution reverted";
    child->output = "0xdeadbeef";
    frame.calls.push_back(std::move(child));

    Json::Value params(Json::arrayValue);
    params.append(crypto::HashType{42U}.hexPrefixed());
    Json::Value config(Json::objectValue);
    config["tracer"] = "callTracer";
    config["timeout"] = "1s250ms";
    config["tracerConfig"]["onlyTopCall"] = true;
    params.append(config);
    Json::Value response;
    task::syncWait(endpoint.traceTransaction(params, response));
    BOOST_CHECK(tracing->observedHash == crypto::HashType{42U});
    BOOST_CHECK_EQUAL(tracing->observedOptions.timeout.count(), 1250);
    BOOST_CHECK(tracing->observedOptions.onlyTopCall);
    BOOST_CHECK_EQUAL(response["result"]["type"].asString(), "CALL");
    BOOST_CHECK(!response["result"].isMember("output"));
    BOOST_CHECK(!response["result"].isMember("error"));
    auto const& childJson = response["result"]["calls"][0U];
    BOOST_CHECK_EQUAL(childJson["error"].asString(), "execution reverted");
    BOOST_CHECK_EQUAL(childJson["output"].asString(), "0xdeadbeef");
    BOOST_CHECK(!childJson.isMember("value"));

    for (auto const& duration :
        {"0s", "31s", "-1s", "nan", "", "1e3ms", "1", "+", "-0.001s", "0.9999999ms", "999999ns",
            "30.000000001s", "30s1ns", "999999999999999999999999999999999999h"})
    {
        BOOST_TEST_CONTEXT(duration)
        {
            auto const invocations = tracing->invocations;
            params[1U]["timeout"] = duration;
            BOOST_CHECK_EXCEPTION(task::syncWait(endpoint.traceTransaction(params, response)),
                JsonRpcException, [](auto const& e) { return e.code() == InvalidParams; });
            BOOST_CHECK_EQUAL(tracing->invocations, invocations);
        }
    }
    for (auto const& [duration, expected] :
        {std::pair<std::string_view, int64_t>{"0.001s", 1}, {"+0.001s", 1}, {"1ms", 1},
            {"1000us", 1}, {"1000µs", 1}, {"1000μs", 1}, {"1000000ns", 1}, {".0005s500us", 1},
            {"1.0000009ms", 1}, {"1.000001ms", 2}, {"999999ns1.9ns", 1}, {"1ms0.9ns0.9ns", 1},
            {"1ms1ns", 2}, {"0.5m", 30000}, {"30s", 30000}, {"30000ms", 30000},
            {"29s999.999999ms1ns", 30000}, {"30.0000000009s", 30000}})
    {
        BOOST_TEST_CONTEXT(duration)
        {
            params[1U]["timeout"] = std::string(duration);
            task::syncWait(endpoint.traceTransaction(params, response));
            BOOST_CHECK_EQUAL(tracing->observedOptions.timeout.count(), expected);
        }
    }
    params[1U].removeMember("timeout");
    params[1U].removeMember("tracerConfig");
    task::syncWait(endpoint.traceTransaction(params, response));
    BOOST_CHECK_EQUAL(tracing->observedOptions.timeout.count(), 5000);
    BOOST_CHECK(!tracing->observedOptions.onlyTopCall);

    auto valid = params;
    auto const invocationsBeforeInvalidOptions = tracing->invocations;
    for (auto const& badTracer : {"prestateTracer", "", "{}"})
    {
        params[1U]["tracer"] = badTracer;
        BOOST_CHECK_EXCEPTION(task::syncWait(endpoint.traceTransaction(params, response)),
            JsonRpcException, [](auto const& e) { return e.code() == InvalidParams; });
    }
    params = valid;
    params[1U]["tracerConfig"]["onlyTopCall"] = "true";
    BOOST_CHECK_EXCEPTION(task::syncWait(endpoint.traceTransaction(params, response)),
        JsonRpcException, [](auto const& e) { return e.code() == InvalidParams; });
    params = valid;
    params[0U] = "0x12";
    BOOST_CHECK_EXCEPTION(task::syncWait(endpoint.traceTransaction(params, response)),
        JsonRpcException, [](auto const& e) { return e.code() == InvalidParams; });
    BOOST_CHECK_EQUAL(tracing->invocations, invocationsBeforeInvalidOptions);

    tracing->error = BCOS_ERROR_PTR(-1, "historical state unavailable");
    BOOST_CHECK_EXCEPTION(task::syncWait(endpoint.traceTransaction(valid, response)),
        JsonRpcException, [](auto const& e) {
            return e.code() == -32000 && e.msg() == "historical state unavailable";
        });
    tracing->error = BCOS_ERROR_PTR(
        bcos::scheduler::SchedulerError::MPTStateUnavailable, "historical state unavailable");
    BOOST_CHECK_EXCEPTION(task::syncWait(endpoint.traceTransaction(valid, response)),
        JsonRpcException, [](auto const& e) { return e.code() == -32004; });
    tracing->error.reset();
    // JSON escaping must count towards the output cap, not just the raw revert text.
    frame.revertReason.assign(2 * 1024 * 1024, '\n');
    BOOST_CHECK_EXCEPTION(task::syncWait(endpoint.traceTransaction(valid, response)),
        JsonRpcException, [](auto const& e) {
            return e.code() == -32000 && e.msg() == "trace output limit exceeded";
        });
}

// debug_dbGet must be registered in EndpointsMapping, but only behind the per-listener
// enable_debug_api gate — a default mapping must NOT dispatch it (the gate is the protection).
BOOST_AUTO_TEST_CASE(DbGetMethodRegistered)
{
    EndpointsMapping const defaultMapping;
    BOOST_CHECK(!defaultMapping.findHandler("debug_dbGet").has_value());

    EndpointsMapping const mapping(/*enableOPEngine=*/false, /*enableMinerApi=*/false,
        /*enableDebugApi=*/true);
    BOOST_CHECK(mapping.findHandler("debug_dbGet").has_value());
    BOOST_CHECK(!mapping.findHandler("debug_dbGetX").has_value());
}

// debug_getRawHeader must be registered in EndpointsMapping, only behind enable_debug_api.
BOOST_AUTO_TEST_CASE(GetRawHeaderMethodRegistered)
{
    EndpointsMapping const defaultMapping;
    BOOST_CHECK(!defaultMapping.findHandler("debug_getRawHeader").has_value());

    EndpointsMapping const mapping(/*enableOPEngine=*/false, /*enableMinerApi=*/false,
        /*enableDebugApi=*/true);
    BOOST_CHECK(mapping.findHandler("debug_getRawHeader").has_value());
}

// debug_dbGet on a 32-byte state node hash returns the injected node preimage.
BOOST_AUTO_TEST_CASE(DbGetStateNodeHappyPath)
{
    wireReader();

    bcos::h256 nodeHash{0x42U};
    bcos::bytes nodeBytes{0xde, 0xad, 0xbe, 0xef};
    task::syncWait(bcos::storage2::writeOne(m_mptNodes, nodeHash, nodeBytes));

    auto resp = call("debug_dbGet", [&] {
        Json::Value p(Json::arrayValue);
        p.append(nodeHash.hexPrefixed());
        return p;
    }());
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_CHECK(resp["result"].asString() == "0xdeadbeef");
}

// debug_dbGet on the 33-byte "c" + codeHash form returns the injected contract code.
BOOST_AUTO_TEST_CASE(DbGetCodePrefixHappyPath)
{
    wireReader();
    bcos::h256 codeHash{0x11U};
    bcos::bytes codeBytes{0x60, 0x00, 0x60, 0x01};
    m_ledger->setStateStorageEntry(std::string(bcos::ledger::SYS_CODE_BINARY),
        codeHash.toRawString(), bcos::storage::Entry{codeBytes});

    // key = "0x63" ('c') + codeHash hex
    std::string key = "0x63" + codeHash.hex();
    auto resp = call("debug_dbGet", [&] {
        Json::Value p(Json::arrayValue);
        p.append(key);
        return p;
    }());
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));
    BOOST_CHECK(resp["result"].asString() == "0x60006001");
}

// debug_dbGet on an unknown 32-byte hash answers -32000 "not found" (geth's server-error code).
BOOST_AUTO_TEST_CASE(DbGetUnknownStateNodeReturnsError)
{
    wireReader();

    bcos::h256 unknown{0x99U};
    auto resp = call("debug_dbGet", [&] {
        Json::Value p(Json::arrayValue);
        p.append(unknown.hexPrefixed());
        return p;
    }());
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32000);
}

// debug_dbGet on a key that is neither the 33-byte "c"+codeHash form nor a bare 32-byte hash
// answers -32000 "not found" (the key shape only picks which store to try first).
BOOST_AUTO_TEST_CASE(DbGetInvalidKeyLengthReturnsNotFoundError)
{
    auto resp = call("debug_dbGet", [&] {
        Json::Value p(Json::arrayValue);
        p.append("0x1234");  // 2 bytes
        return p;
    }());
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32000);
}

// debug_dbGet on a 33-byte key whose leading byte is not 'c' answers -32000 "not found" — it is
// not the code-key form, and its length is not a bare 32-byte hash either.
BOOST_AUTO_TEST_CASE(DbGetNonCPrefixKeyReturnsNotFoundError)
{
    // 33 bytes = 66 hex chars, leading byte 0xdd (not 0x63 == 'c').
    std::string key = "0xdd" + std::string(64, '0');
    auto resp = call("debug_dbGet", [&] {
        Json::Value p(Json::arrayValue);
        p.append(key);
        return p;
    }());
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32000);
}

// debug_getRawHeader on a malformed hash answers -32602 InvalidParams.
BOOST_AUTO_TEST_CASE(GetRawHeaderMalformedHashReturnsInvalidParams)
{
    std::string malformed = "0x" + std::string(64, 'z');
    auto resp = call("debug_getRawHeader", [&] {
        Json::Value p(Json::arrayValue);
        p.append(malformed);
        return p;
    }());
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32602);
    BOOST_CHECK(
        resp["error"]["message"].asString().find("Invalid block hash") != std::string::npos);
}

// debug_getRawHeader on an unknown hash answers -32602 "Block not found".
BOOST_AUTO_TEST_CASE(GetRawHeaderUnknownHashReturnsBlockNotFound)
{
    std::string unknown = "0x" + std::string(64, '0');
    auto resp = call("debug_getRawHeader", [&] {
        Json::Value p(Json::arrayValue);
        p.append(unknown);
        return p;
    }());
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32602);
    BOOST_CHECK(resp["error"]["message"].asString().find("Block not found") != std::string::npos);
}

// debug_getRawHeader happy path: the returned RLP's keccak256 equals the requested block hash —
// the exact byte-level identity the preimage-oracle key demands.
BOOST_AUTO_TEST_CASE(GetRawHeaderEthHeaderHappyPath)
{
    auto header = makeEthHeader();
    bcos::protocol::EthBlockHeader::calculateRLPHash(*header);
    m_ledger->injectBlock(header);

    auto const hash = header->hash();
    auto resp = call("debug_getRawHeader", [&] {
        Json::Value p(Json::arrayValue);
        p.append(hash.hexPrefixed());
        return p;
    }());
    BOOST_TEST(!resp.isMember("error"));
    BOOST_REQUIRE(resp.isMember("result"));

    auto rlp = bcos::fromHexWithPrefix(resp["result"].asString());
    BOOST_CHECK(!rlp.empty());
    auto recomputed = bcos::crypto::keccak256Hash(bcos::ref(rlp));
    BOOST_CHECK(recomputed == hash);
}

// debug_getRawHeader on a FISCO-native header answers -32603 — the native header has no
// Ethereum RLP (its millisecond timestamp fails the EthBlockHeader bridge, or its published
// hash is not keccak of an RLP header), so serving it would hand kona-host bytes that do not
// hash to the published hash.
BOOST_AUTO_TEST_CASE(GetRawHeaderNativeHeaderEncodeFailure)
{
    // The FakeLedger default headers are FISCO-native (EthBlockVersion::NON_ETH) with a
    // millisecond timestamp, which the EthBlockHeader bridge rejects. Capture the latest hash
    // first: the lookup resolves, then the encode fails.
    auto const latestHash = m_ledger->ledgerData().back()->blockHeader()->hash();
    auto resp = call("debug_getRawHeader", [&] {
        Json::Value p(Json::arrayValue);
        p.append(latestHash.hexPrefixed());
        return p;
    }());
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32603);
    BOOST_CHECK(
        resp["error"]["message"].asString().find("Header RLP encode failed") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
