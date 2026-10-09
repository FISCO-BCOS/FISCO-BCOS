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
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <future>
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
        web3JsonRpc->onRPCRequest(req,
            [&promise](bcos::bytes resp, boost::beast::http::status) {
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
    bcos::h256 codeHash{0x11U};
    bcos::bytes codeBytes{0x60, 0x00, 0x60, 0x01};
    m_ledger->setStateStorageEntry(
        std::string(bcos::ledger::SYS_CODE_BINARY), codeHash.toRawString(),
        bcos::storage::Entry{codeBytes});

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

// debug_dbGet on an unknown 32-byte hash answers -32603 "not found".
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
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32603);
}

// debug_dbGet on a key that is neither 32 nor 33 bytes answers -32602 InvalidParams.
BOOST_AUTO_TEST_CASE(DbGetInvalidKeyLengthReturnsInvalidParams)
{
    auto resp = call("debug_dbGet", [&] {
        Json::Value p(Json::arrayValue);
        p.append("0x1234");  // 2 bytes
        return p;
    }());
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32602);
}

// debug_dbGet on a 33-byte key whose leading byte is not 'c' answers -32602 with the
// distinct non-'c'-prefix message (not the length-mismatch message).
BOOST_AUTO_TEST_CASE(DbGetNonCPrefixKeyReturnsInvalidParams)
{
    // 33 bytes = 66 hex chars, leading byte 0xdd (not 0x63 == 'c').
    std::string key = "0xdd" + std::string(64, '0');
    auto resp = call("debug_dbGet", [&] {
        Json::Value p(Json::arrayValue);
        p.append(key);
        return p;
    }());
    BOOST_REQUIRE(resp.isMember("error"));
    BOOST_CHECK_EQUAL(resp["error"]["code"].asInt(), -32602);
    BOOST_CHECK(resp["error"]["message"].asString().find("33-byte \"c\"+codeHash form") !=
                std::string::npos);
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

// debug_getRawHeader on a FISCO-native header (non-whole-second timestamp) answers -32603 —
// the EthBlockHeader bridge rejects the sub-second millisecond timestamp.
BOOST_AUTO_TEST_CASE(GetRawHeaderNativeHeaderEncodeFailure)
{
    // The FakeLedger default headers carry a millisecond timestamp (utcTime()), which the
    // EthBlockHeader ctor rejects. Capture the latest hash first: the header's hash is the
    // FISCO-native hash, so the lookup resolves, then the RLP encode throws.
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
