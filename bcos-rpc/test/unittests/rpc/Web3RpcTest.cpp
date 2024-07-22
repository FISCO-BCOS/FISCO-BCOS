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
 * @file Web3RpcTest.cpp
 * @author: kyonGuo
 * @date 2024/3/29
 */


#include "../common/RPCFixture.h"
#include "bcos-crypto/encrypt/AESCrypto.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h"
#include "bcos-crypto/signature/sm2/SM2Crypto.h"
#include "bcos-crypto/signature/sm2/SM2KeyPair.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-rpc/bcos-rpc/RpcFactory.h"

#include <bcos-codec/wrapper/CodecWrapper.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/testutils/faker/FakeFrontService.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-framework/testutils/faker/FakeSealer.h>
#include <bcos-rpc/tarsRPC/RPCServer.h>
#include <bcos-rpc/validator/CallValidator.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

#include <bcos-rpc/filter/LogMatcher.h>
#include <bcos-rpc/web3jsonrpc/model/Web3FilterRequest.h>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::crypto;
namespace bcos::test
{
class Web3TestFixture : public RPCFixture
{
public:
    Web3TestFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_CHECK(web3JsonRpc != nullptr);
    }
    Json::Value onRPCRequestWrapper(std::string_view request, rpc::Sender _diySender = nullptr)
    {
        Json::Value value;
        Json::Reader reader;
        if (!_diySender)
        {
            std::promise<bcos::bytes> promise;
            web3JsonRpc->onRPCRequest(
                request, [&promise](bcos::bytes resp) { promise.set_value(std::move(resp)); });
            auto jsonBytes = promise.get_future().get();
            std::string_view json(
                (char*)jsonBytes.data(), (char*)jsonBytes.data() + jsonBytes.size());
            reader.parse(json.begin(), json.end(), value);
        }
        else
        {
            web3JsonRpc->onRPCRequest(request, std::move(_diySender));
        }
        return value;
    }
    static void validRespCheck(Json::Value const& resp)
    {
        BOOST_CHECK(!resp.isMember("error"));
        BOOST_CHECK(resp.isMember("result"));
        BOOST_CHECK(resp.isMember("id"));
        BOOST_CHECK(resp.isMember("jsonrpc"));
        BOOST_CHECK(resp["jsonrpc"].asString() == "2.0");
    };

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};
BOOST_FIXTURE_TEST_SUITE(testWeb3RPC, Web3TestFixture)
BOOST_AUTO_TEST_CASE(handleInvalidTest)
{
    // no method
    {
        const auto request = R"({"jsonrpc":"2.0","id":1})";
        auto response = onRPCRequestWrapper(request);
        BOOST_CHECK(response.isMember("error"));
        BOOST_CHECK(response["error"]["code"].asInt() == InvalidRequest);
    }

    // invalid json
    {
        const auto request = R"({{"jsonrpc":"2.0","id":1, "method":"eth_blockNumber","params":[]})";
        auto response = onRPCRequestWrapper(request);
        BOOST_CHECK(response.isMember("error"));
        BOOST_CHECK(response["error"]["code"].asInt() == InvalidRequest);
    }

    // invalid params type
    {
        const auto request = R"({"jsonrpc":"2.0","id":1, "method":"eth_blockNumber","params":{}})";
        auto response = onRPCRequestWrapper(request);
        BOOST_CHECK(response.isMember("error"));
        BOOST_CHECK(response["error"]["code"].asInt() == InvalidRequest);
    }

    // invalid method
    {
        const auto request = R"({"jsonrpc":"2.0","id":1, "method":"eth_AAA","params":[]})";
        auto response = onRPCRequestWrapper(request);
        BOOST_CHECK(response.isMember("error"));
        BOOST_CHECK(response["error"]["code"].asInt() == MethodNotFound);
        BOOST_CHECK(response["error"]["message"].asString() == "Method not found");
    }

    // not impl method
    {
        const auto request = R"({"jsonrpc":"2.0","id":1, "method":"eth_coinbase","params":[]})";
        auto response = onRPCRequestWrapper(request);
        std::string s = response.toStyledString();
        BOOST_CHECK(response.isMember("error"));
        BOOST_CHECK(response["error"]["code"].asInt() == MethodNotFound);
        BOOST_CHECK(
            response["error"]["message"].asString() == "This API has not been implemented yet!");
    }
}

BOOST_AUTO_TEST_CASE(handleValidTest)
{
    // method eth_syncing
    // {
    //     const auto request =
    //         R"({"jsonrpc":"2.0","id":1132123, "method":"eth_syncing","params":[]})";
    //     auto response = onRPCRequestWrapper(request);
    //     validRespCheck(response);
    //     BOOST_CHECK(response["id"].asInt64() == 1132123);
    //     BOOST_CHECK(response["result"].asBool() == false);
    // }

    // method eth_chainId
    {
        const auto request = R"({"jsonrpc":"2.0","id":123, "method":"eth_chainId","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 123);
        BOOST_CHECK(fromQuantity(response["result"].asString()) == 0);
    }

    // method eth_mining
    {
        const auto request = R"({"jsonrpc":"2.0","id":3214, "method":"eth_mining","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 3214);
        BOOST_CHECK(response["result"].asBool() == false);
    }

    // method eth_hashrate
    {
        const auto request = R"({"jsonrpc":"2.0","id":3214, "method":"eth_hashrate","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 3214);
        BOOST_CHECK(fromQuantity(response["result"].asString()) == 0);
    }

    // method eth_gasPrice
    {
        m_ledger->setSystemConfig(SYSTEM_KEY_TX_GAS_PRICE, "0x99e670");
        const auto request =
            R"({"jsonrpc":"2.0","id":541321, "method":"eth_gasPrice","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 541321);
        auto b = fromQuantity(response["result"].asString());
        std::cout << b << std::endl;
        BOOST_CHECK(b == 10086000);
        BOOST_CHECK(fromQuantity(response["result"].asString()) == 10086000);
    }

    // method eth_accounts
    {
        const auto request = R"({"jsonrpc":"2.0","id":3214, "method":"eth_accounts","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 3214);
        BOOST_CHECK(response["result"].size() == 0);
    }

    // method eth_blockNumber
    {
        const auto request =
            R"({"jsonrpc":"2.0","id":996886, "method":"eth_blockNumber","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 996886);
        auto const blkNum = toQuantity(m_ledger->blockNumber());
        BOOST_CHECK(response["result"].asString() == blkNum);
    }

    // method eth_blockNumber
    {
        const auto request =
            R"({"id":1655516568316917,"jsonrpc":"2.0","method":"eth_blockNumber","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asUInt64() == 1655516568316917);
        auto const blkNum = toQuantity(m_ledger->blockNumber());
        BOOST_CHECK(response["result"].asString() == blkNum);
    }

    // method eth_blockNumber
    {
        const auto request =
            R"({"id":"e4df2b99-f80b-4e23-91c7-b9471b46af26","jsonrpc":"2.0","method":"eth_blockNumber","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asString() == "e4df2b99-f80b-4e23-91c7-b9471b46af26");
        auto const blkNum = toQuantity(m_ledger->blockNumber());
        BOOST_CHECK(response["result"].asString() == blkNum);
    }
}

BOOST_AUTO_TEST_CASE(handleWeb3NamespaceValidTest)
{
    auto validRespCheck = [](Json::Value const& resp) {
        BOOST_CHECK(!resp.isMember("error"));
        BOOST_CHECK(resp.isMember("result"));
        BOOST_CHECK(resp.isMember("id"));
        BOOST_CHECK(resp.isMember("jsonrpc"));
        BOOST_CHECK(resp["jsonrpc"].asString() == "2.0");
    };

    // method web3_clientVersion
    {
        const auto request =
            R"({"jsonrpc":"2.0","id":24243, "method":"web3_clientVersion","params":[]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 24243);
        std::string result = response["result"].asString();
        BOOST_CHECK(result.find("FISCO-BCOS-Web3RPC") != std::string::npos);
    }

    // method web3_sha3
    {
        const auto request =
            R"({"jsonrpc":"2.0","id":321314, "method":"web3_sha3","params":["0x68656c6c6f20776f726c64"]})";
        auto response = onRPCRequestWrapper(request);
        validRespCheck(response);
        BOOST_CHECK(response["id"].asInt64() == 321314);
        BOOST_CHECK(response["result"].asString() ==
                    "0x47173285a8d7341e5e972fc677286384f802f8ef42a5ec5f03bbfa254cb01fad");
    }
}

BOOST_AUTO_TEST_CASE(handleLegacyTxTest)
{
    // method eth_sendRawTransaction
    auto testLegacyTx = [&](std::string const& rawTx, std::string const& expectHash) {
        // clang-format off
        const std::string request = R"({"jsonrpc":"2.0","id":1132123, "method":"eth_sendRawTransaction","params":[")" + rawTx + R"("]})";
        // clang-format on
        auto rawTxBytes = fromHexWithPrefix(rawTx);
        auto rawTxBytesRef = bcos::ref(rawTxBytes);
        Web3Transaction rawWeb3Tx;
        codec::rlp::decode(rawTxBytesRef, rawWeb3Tx);
        onRPCRequestWrapper(request, [](auto&&) {});
        // validRespCheck(response);
        // BOOST_CHECK(response["id"].asInt64() == 1132123);
        // BOOST_CHECK(response["result"].asString() == expectHash);
        std::vector<crypto::HashType> hashes = {HashType(expectHash)};
        task::wait([](Web3TestFixture* self, decltype(hashes) m_hashes,
                       decltype(rawWeb3Tx) rawWeb3Tx) -> task::Task<void> {
            auto txs = co_await self->txPool->getTransactions(m_hashes);
            BOOST_CHECK(txs.size() == 1);
            BOOST_CHECK(txs[0]->hash() == m_hashes[0]);
            BOOST_CHECK(txs[0]->type() == 1);
            BOOST_CHECK(!txs[0]->extraTransactionBytes().empty());
            auto ref = bytesRef(const_cast<unsigned char*>(txs[0]->extraTransactionBytes().data()),
                txs[0]->extraTransactionBytes().size());
            auto const chainId = txs[0]->chainId();
            BOOST_CHECK(chainId == std::to_string(rawWeb3Tx.chainId.value_or(0)));
            Web3Transaction tx;
            bcos::codec::rlp::decode(ref, tx);
            BOOST_CHECK(tx.type == rawWeb3Tx.type);
            BOOST_CHECK(tx.data == rawWeb3Tx.data);
            BOOST_CHECK(tx.nonce == rawWeb3Tx.nonce);
            BOOST_CHECK(tx.to == rawWeb3Tx.to);
            BOOST_CHECK(tx.value == rawWeb3Tx.value);
            BOOST_CHECK(tx.maxFeePerGas == rawWeb3Tx.maxFeePerGas);
            BOOST_CHECK(tx.maxPriorityFeePerGas == rawWeb3Tx.maxPriorityFeePerGas);
            BOOST_CHECK(tx.gasLimit == rawWeb3Tx.gasLimit);
            BOOST_CHECK(tx.accessList == rawWeb3Tx.accessList);
        }(this, std::move(hashes), std::move(rawWeb3Tx)));
    };

    // clang-format off
    // https://etherscan.io/tx/0x6f55e12280765243993c76e80e244ecad548aff22380c8e89e060a60deab4b28
    testLegacyTx(
        "0xf902ee83031ae9850256506a88831b40d094d56e4eab23cb81f43168f9f45211eb027b9ac7cc80b90284b143044b00000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000650000000000000000000000004d73adb72bc3dd368966edd0f0b2148401a178e200000000000000000000000000000000000000000000000000000000000000a000000000000000000000000000000000000000000000000000000000661d084300000000000000000000000000000000000000000000000000000000000001600000000000000000000000000000000000000000000000000000000000000084704316e5000000000000000000000000000000000000000000000000000000000000006e740e551c6ee78d757128b8e476b390f891c54e96538e1bb4469a62105220215a0000000000000000000000000000000000000000000000000000000000000014740e551c6ee78d757128b8e476b390f891c54e96538e1bb4469a62105220215a000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000082ce44cffc92754f8825e1c60cadc5f54a504b769ee616e9f28a9797c2a4b84d11542a1aa4a06a6aa60ee062a36c4fb467145b8914959e42323a13068e2475bff41c67af8dd96034675776cb78036711c1f320b82085df5ee005ffca77959f29a0a07a226241787f06b57483d509e0befd84e5ac84d960e881c7b0853ee1d7c63dff1b00000000000000000000000000000000000000000000000000000000000026a04932608e9743aaa76626f082cedb5da11ff8a1ebe6b5a62a372c81393b5912aea012f36de80608ba3b0f72f3e8f299379ce2802e64b1cbb55275ad9aaa81190b44",
        "0x6f55e12280765243993c76e80e244ecad548aff22380c8e89e060a60deab4b28");
    // https://etherscan.io/tx/0x4cbe0f1e383e4aec60cedd0e8dc01832efd160415d82557a72853ec1a58e615b
    testLegacyTx("0xf9014d8203ca8504a817c800830f42408080b8f9606060405260008054600160a060020a0319163317905560d78060226000396000f36060604052361560275760e060020a600035046341c0e1b58114606e578063e5225381146096575b60d56000341115606c57346060908152605890600160a060020a033316907f90890809c654f11d6e72a28fa60149770a0d11ec6c92319d6ceb2bb0a4ea1a1590602090a35b565b60d5600054600160a060020a0390811633919091161415606c57600054600160a060020a0316ff5b60d5600054600160a060020a0390811633919091161415606c5760008054600160a060020a039081169190301631606082818181858883f15050505050565b001ca0c11d1e703f19bd676fcd8cb0c2f6d9febd4c117692215e92a8933f0c34e2ea85a03a9cd0c207899aa699d287025fa13073a461485a59a4fe0aa246db9a2428e7a7",
        "0x4cbe0f1e383e4aec60cedd0e8dc01832efd160415d82557a72853ec1a58e615b");
    // https://etherscan.io/tx/0xdd2294fae0bce9409e3f52684b9947613147b9fe2fcb8efb648ca096728236f5
    testLegacyTx("0xf9014d8203bb8504a817c800830f42408080b8f9606060405260008054600160a060020a0319163317905560d78060226000396000f36060604052361560275760e060020a600035046341c0e1b58114606e578063e5225381146096575b60d56000341115606c57346060908152605890600160a060020a033316907f90890809c654f11d6e72a28fa60149770a0d11ec6c92319d6ceb2bb0a4ea1a1590602090a35b565b60d5600054600160a060020a0390811633919091161415606c57600054600160a060020a0316ff5b60d5600054600160a060020a0390811633919091161415606c5760008054600160a060020a039081169190301631606082818181858883f15050505050565b001ca0e97321e5cdb2d677b840337568b057cdf9748659f47fc92e3d29c5d418c36f01a02c2eb384060b1784c0c5b4029d704bc75ae9cbcd17994db535e2cc4863ec1973",
        "0xdd2294fae0bce9409e3f52684b9947613147b9fe2fcb8efb648ca096728236f5");
    // clang-format on
}

BOOST_AUTO_TEST_CASE(handleEIP1559TxTest)
{
    // method eth_sendRawTransaction
    auto testEIP1559Tx = [&](std::string const& rawTx, std::string const& expectHash) {
        // clang-format off
        const std::string request = R"({"jsonrpc":"2.0","id":1132123, "method":"eth_sendRawTransaction","params":[")" + rawTx + R"("]})";
        // clang-format on
        auto rawTxBytes = fromHexWithPrefix(rawTx);
        auto rawTxBytesRef = bcos::ref(rawTxBytes);
        Web3Transaction rawWeb3Tx;
        codec::rlp::decode(rawTxBytesRef, rawWeb3Tx);
        onRPCRequestWrapper(request, [](auto&&) {});
        // validRespCheck(response);
        // BOOST_CHECK(response["id"].asInt64() == 1132123);
        // BOOST_CHECK(response["result"].asString() == expectHash);
        std::vector<crypto::HashType> hashes = {HashType(expectHash)};
        task::wait([&rawWeb3Tx](
                       Web3TestFixture* self, decltype(hashes) m_hashes) -> task::Task<void> {
            auto txs = co_await self->txPool->getTransactions(m_hashes);
            BOOST_CHECK(txs.size() == 1);
            BOOST_CHECK(txs[0] != nullptr);
            BOOST_CHECK(txs[0]->hash() == m_hashes[0]);
            BOOST_CHECK(txs[0]->type() == 1);
            BOOST_CHECK(!txs[0]->extraTransactionBytes().empty());
            auto ref = bytesRef(const_cast<unsigned char*>(txs[0]->extraTransactionBytes().data()),
                txs[0]->extraTransactionBytes().size());
            Web3Transaction tx;
            bcos::codec::rlp::decode(ref, tx);
            BOOST_CHECK(tx.type == rawWeb3Tx.type);
            BOOST_CHECK(tx.data == rawWeb3Tx.data);
            BOOST_CHECK(tx.nonce == rawWeb3Tx.nonce);
            BOOST_CHECK(tx.to == rawWeb3Tx.to);
            BOOST_CHECK(tx.value == rawWeb3Tx.value);
            BOOST_CHECK(tx.maxFeePerGas == rawWeb3Tx.maxFeePerGas);
            BOOST_CHECK(tx.maxPriorityFeePerGas == rawWeb3Tx.maxPriorityFeePerGas);
            BOOST_CHECK(tx.gasLimit == rawWeb3Tx.gasLimit);
            BOOST_CHECK(tx.accessList == rawWeb3Tx.accessList);
            BOOST_CHECK(tx.chainId == rawWeb3Tx.chainId);
        }(this, std::move(hashes)));
        return rawWeb3Tx;
    };

    // clang-format off
    // https://etherscan.io/tx/0x5b2f242c755ec9f9ed36628331991bd6c90b712b5867a0c7f3a4516caf09cc68
    testEIP1559Tx(
        "0x02f871018308b3e6808501cd2ec1d7826ac194ba1951df0c0a52af23857c5ab48b4c43a57e7ed1872700f2d0ba3db080c001a069be171dfa805790a28f1bfcd131eb2aa8f345f601c4a3659de4ae8d624a7b89a06e0f6ed7d035397547aeac0e5130847570f4b607350f71c1391b7cb7f9dd604c",
        "0x5b2f242c755ec9f9ed36628331991bd6c90b712b5867a0c7f3a4516caf09cc68");
    // https://etherscan.io/tx/0x698f4dc5e9948d62733d84c4adce663e06fe9c2a42c1bce9dc675fc0ae66cfdc
    testEIP1559Tx("0x02f8b00181c4830ecd1085026856b07782b744944c9edd5852cd905f086c759e8383e09bff1e68b380b844095ea7b3000000000000000000000000000000000022d473030f116ddee9f6b43ac78ba3ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffc080a0170f981789f3265ec823403335db3c9da85a191720417b9d29253cbd56e6a3c9a05f8dd3422141cc9c158a776069769fd6b6ac35d1a9c6a2b3d3722b28fd5f9759",
        "0x698f4dc5e9948d62733d84c4adce663e06fe9c2a42c1bce9dc675fc0ae66cfdc");
    // https://etherscan.io/tx/0x91d638aec203ed00f60ac4b89916d6a79532a6acc24088286891c978d66106ce
    testEIP1559Tx("0x02f8b3018310468983e4e1c085746a528800830249f094a0b86991c6218b36c1d19d4a2e9eb0ce3606eb4880b844a9059cbb000000000000000000000000a4909347fe9cfd582a382253fc4a5710d32c016b0000000000000000000000000000000000000000000000000000000042968240c080a06d687ed8689c5f022c97cb4be0053d01e2d7c03236fc4bc5765169a29600fa2ca023bee5a2365088addf6ac5844d6ccfebdeddb979165aaa7371813eb1b3a89e37",
        "0x91d638aec203ed00f60ac4b89916d6a79532a6acc24088286891c978d66106ce");
    // clang-format on
}

BOOST_AUTO_TEST_CASE(handleEIP4844TxTest)
{
    // method eth_sendRawTransaction
    auto testEIP4844Tx = [&](std::string const& rawTx, std::string const& expectHash) {
        // clang-format off
        const std::string request = R"({"jsonrpc":"2.0","id":1132123, "method":"eth_sendRawTransaction","params":[")" + rawTx + R"("]})";
        // clang-format on
        auto rawTxBytes = fromHexWithPrefix(rawTx);
        auto rawTxBytesRef = bcos::ref(rawTxBytes);
        Web3Transaction rawWeb3Tx;
        codec::rlp::decode(rawTxBytesRef, rawWeb3Tx);
        onRPCRequestWrapper(request, [](auto&&) {});
        // validRespCheck(response);
        // BOOST_CHECK(response["id"].asInt64() == 1132123);
        // BOOST_CHECK(response["result"].asString() == expectHash);
        std::vector<crypto::HashType> hashes = {HashType(expectHash)};
        task::wait([&rawWeb3Tx](
                       Web3TestFixture* self, decltype(hashes) m_hashes) -> task::Task<void> {
            auto txs = co_await self->txPool->getTransactions(m_hashes);
            BOOST_CHECK(txs.size() == 1);
            BOOST_CHECK(txs[0]->hash() == m_hashes[0]);
            BOOST_CHECK(txs[0]->type() == 1);
            BOOST_CHECK(!txs[0]->extraTransactionBytes().empty());
            auto ref = bytesRef(const_cast<unsigned char*>(txs[0]->extraTransactionBytes().data()),
                txs[0]->extraTransactionBytes().size());
            Web3Transaction tx;
            bcos::codec::rlp::decode(ref, tx);
            BOOST_CHECK(tx.type == rawWeb3Tx.type);
            BOOST_CHECK(tx.data == rawWeb3Tx.data);
            BOOST_CHECK(tx.nonce == rawWeb3Tx.nonce);
            BOOST_CHECK(tx.to == rawWeb3Tx.to);
            BOOST_CHECK(tx.value == rawWeb3Tx.value);
            BOOST_CHECK(tx.maxFeePerGas == rawWeb3Tx.maxFeePerGas);
            BOOST_CHECK(tx.maxPriorityFeePerGas == rawWeb3Tx.maxPriorityFeePerGas);
            BOOST_CHECK(tx.gasLimit == rawWeb3Tx.gasLimit);
            BOOST_CHECK(tx.accessList == rawWeb3Tx.accessList);
            BOOST_CHECK(tx.chainId == rawWeb3Tx.chainId);
            BOOST_CHECK(tx.maxFeePerBlobGas == rawWeb3Tx.maxFeePerBlobGas);
            BOOST_CHECK(tx.blobVersionedHashes == rawWeb3Tx.blobVersionedHashes);
        }(this, std::move(hashes)));
        return rawWeb3Tx;
    };

    // clang-format off
    // https://etherscan.io/tx/0x637b7e88d7a0992b62a973acd41736ee2b63be1c86d0ffeb683747964daea892
    testEIP4844Tx(
        "0x03f902fd018309a7ee8405f5e1008522ecb25c008353ec6094c662c410c0ecf747543f5ba90660f6abebd9c8c480b90264b72d42a100000000000000000000000000000000000000000000000000000000000000400000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000000d06ad5334c4498113f3cc1548635e7c32bb72562421fddef1e60d165d8035ebe2031fe8a2addb2bec3c5c43b6168e3dac21f84cddbfae7d9e24977fbee8038772000000000000000000000000000000000000000000000000000000000009a7ed04ef432aa69fe0dd81c5d5d07464120008bbaede9cf73c4ca149f0be56acfbf605ba2078240f1585f96424c2d1ee48211da3b3f9177bf2b9880b4fc91d59e9a200000000000000000000000000000000000000000000000000000000000000010000000000000000df5459e0fefcbcfb5585179c4dfde48bc334f5836ad2821f0000000000000000883e48453eb072b0e184b27522f52d8324ee5d6ee778b05002c8df8c56bb3f47ab53b12be77ca1192abf4821a3e2b32c24d34af793fbcdca00000000000000000000000000000000cf420df13fc18924e41f7b0bd34de6a6000000000000000000000000000000000f389c6a65227d0e916c9670e8398e0d00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000003088cf8a7c3410fc132c573b94603dd2cf6e25555808a487c925ae595f6e2d1907907ba57a3fb42729eb1de453ebeda6bc00000000000000000000000000000000c08522ecb25c00e1a00131e940049304bb30d0da71fcccc542712238c5e2aa6ad96cf7eb5fa8ef974880a0f9ea6a59f3f160f36e1e2248085ec9d3bc3797462b4e5fd2c53f6753ddc88cc3a06e0aa62f613475a881564eb9269762b466560d4870835a198e12f6d5e05a9eaa",
        "0x637b7e88d7a0992b62a973acd41736ee2b63be1c86d0ffeb683747964daea892");
    // https://etherscan.io/tx/0x82d366c93c5fb2cbe8f1b6f0f19c260d11a958875560a398d75784ce9f4d6adf
    testEIP4844Tx("0x03f9013b018310fcdb847735940085041e2a063282520894ff000000000000000000000000000000000000108080c0843b9aca00f8c6a001a74d77682287763fc9a460f1c0a2f686daa8bdbb8757564c0fc05f54bbd6d5a0015d0db22bb787710c9d112373a0a49132646f0813c478d15ed247c148982172a00185ece9183ab795a0bfd3326d5b64848d9839992ac3f3ce4a55afb6e7ba4520a0013d1e438e3fb85830cb49bad9598a1ac720b778796c67a00ed8e20eebde05c5a001ff9dae3fb9320795caa1b474a9f667f2022e0e3d8a678ebfe163d7801908c1a001622f10864730dcd603857f00824ad18e070c64b57f9878738db1f85cfad33880a04539a1636f5c431241be963c898bc59491ed7c01853b75ed9e9e9bc47d0aa9ada031880afe746833518e6ea271ecd6a3943fa6da09ba1f889531729a1c8270e7cc",
        "0x82d366c93c5fb2cbe8f1b6f0f19c260d11a958875560a398d75784ce9f4d6adf");
    // https://etherscan.io/tx/0xa8fd95a70b4f2b6cea8c52bcb782b5c3f806a0d5250cf75a1d97a6e899f09979
    testEIP4844Tx("0x03f9013a0182a8f98477359400850432ec4812825208946f54ca6f6ede96662024ffd61bfd18f3f4e34dff8080c0843b9aca00f8c6a001fb60d5b0abeff9e3d47099386a31eed62cd55d54aa146b42d10eb81b0a9b2aa00156b2193030eb7c9496d8586492934a57a5ebd0fac816ef1ea01dc4d09498c5a001bd78704a4b015adec86a654a123045312b083641ababec0e60ef17be4453e7a001b59b9a8b8d35bd6afcff91c6afc56ebcaf4a62f6e83f5e57aac4484f022cc4a00110aac506aff4957e40d4640f0763015b84bbb8c23f50f1b13d501067bea878a001f100a97a70563cd623e588c976155ffe5999d1e9258302d969964d7524bd0280a08c1cff27365c5fe0a6ebfe5e010b3460747489302547f3ba5b181390fad485ada02af2f430e0dfad48ba78853b59486ea7425835c3a7034bb22702234f8994288f",
        "0xa8fd95a70b4f2b6cea8c52bcb782b5c3f806a0d5250cf75a1d97a6e899f09979");
    // clang-format on
}
BOOST_AUTO_TEST_CASE(logMatcherTest)
{
    // clang-format off
    h256 A(fromHexWithPrefix(std::string("0x7ac7a04970319ae8fc5b92fe177d000fee3c00c92f8e78aae13d6571f17c351f")));
    h256 B(fromHexWithPrefix(std::string("0x1fe46fe782fa5618721cdf61de2e50c0639f4b26f6568f9c67b128f5610ced68")));
    h256 C(fromHexWithPrefix(std::string("0x0c5ace674d8e621803f806d73d9d03a49b2694f13186b8d657c9df689fe4e478")));
    h256 D(fromHexWithPrefix(std::string("0xf0173383cebedea9e486292201bc460817279651f0b05c46c0ad1f5e094e62d9")));
    h256 E(fromHexWithPrefix(std::string("0x7b0682ce692786e3ad67f5ff45e7fe63c1dbed629f5f016c605b814d905b6447")));
    h256 F(fromHexWithPrefix(std::string("0xa0f422234f24bda551aee9753bb806b078efa605b128061af1be03e7ce54e64d")));
    h256 G(fromHexWithPrefix(std::string("0xba2b5bce04969801bc79406f18a929f465e83afbb138834d5ef55a0112018a8f")));
    h256 H(fromHexWithPrefix(std::string("0x8518c89a1be62e684eac2fcfad1682ea6fbccff0e436fb7c8ce19c64ad569f79")));
    bytes address1(fromHexWithPrefix(std::string("0x6849f21d1e455e9f0712b1e99fa4fcd23758e8f1")));
    bytes address2(fromHexWithPrefix(std::string("0xc75ebd24f8ae4f17be209b30dc8dea4fadcf67ce")));
    // clang-format on

    LogMatcher matcher;

    // 1.[] "anything"
    {
        // [A, B]
        protocol::LogEntry log1(address1, {A, B}, bytes());
        // [C, D]
        protocol::LogEntry log2(address1, {C, D}, bytes());
        auto params1 = std::make_shared<Web3FilterRequest>();
        auto params2 = std::make_shared<Web3FilterRequest>();
        params2->addAddress(toHexStringWithPrefix(address1));
        BOOST_CHECK(matcher.matches(params1, log1));
        BOOST_CHECK(matcher.matches(params1, log2));
        BOOST_CHECK(!matcher.matches(params2, log1));
        BOOST_CHECK(!matcher.matches(params2, log2));
    }
    // 2.[A] "A in first position (and anything after)"
    {
        auto params = std::make_shared<Web3FilterRequest>();
        params->resizeTopic(1);
        params->addTopic(0, toHexStringWithPrefix(A));
        protocol::LogEntry log;
        // [A]
        log = protocol::LogEntry(address1, {A}, bytes());
        BOOST_CHECK(matcher.matches(params, log));
        // [A, B]
        log = protocol::LogEntry(address1, {A, B}, bytes());
        BOOST_CHECK(matcher.matches(params, log));
        // [C]
        log = protocol::LogEntry(address1, {C}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [C, D]
        log = protocol::LogEntry(address1, {C}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
    }
    // 3.[null, B] "anything in first position AND B in second position (and anything after)"
    {
        auto params = std::make_shared<Web3FilterRequest>();
        params->resizeTopic(2);
        params->addTopic(1, toHexStringWithPrefix(B));
        protocol::LogEntry log;
        // matched
        // [A, B]
        log = protocol::LogEntry(address1, {A, B}, bytes());
        BOOST_CHECK(matcher.matches(params, log));
        // [C, B]
        log = protocol::LogEntry(address1, {C, B}, bytes());
        BOOST_CHECK(matcher.matches(params, log));
        // [A, B, F]
        log = protocol::LogEntry(address1, {A, B, F}, bytes());
        BOOST_CHECK(matcher.matches(params, log));
        // [A, B, E]
        log = protocol::LogEntry(address1, {A, B, E}, bytes());
        BOOST_CHECK(matcher.matches(params, log));
        // [C, B, F]
        log = protocol::LogEntry(address1, {C, B, F}, bytes());
        BOOST_CHECK(matcher.matches(params, log));
        // [C, B, E]
        log = protocol::LogEntry(address1, {C, B, E}, bytes());
        BOOST_CHECK(matcher.matches(params, log));
        // mismatched
        // [A, G]
        log = protocol::LogEntry(address1, {A, G}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [C, G]
        log = protocol::LogEntry(address1, {C, G}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [A, G, F]
        log = protocol::LogEntry(address1, {A, G, F}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [A, G, E]
        log = protocol::LogEntry(address1, {A, G, E}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [C, G, F]
        log = protocol::LogEntry(address1, {C, G, F}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [C, G, E]
        log = protocol::LogEntry(address1, {C, G, E}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
    }
    // 4.[A, B] "A in first position AND B in second position (and anything after)"
    {
        auto params = std::make_shared<Web3FilterRequest>();
        params->resizeTopic(2);
        params->addTopic(0, toHexStringWithPrefix(A));
        params->addTopic(1, toHexStringWithPrefix(B));
        protocol::LogEntry log;

        // matched
        // [A, B]
        log = protocol::LogEntry(address1, {A, B}, bytes());
        BOOST_CHECK(matcher.matches(params, log));
        // [A, B, C]
        log = protocol::LogEntry(address1, {A, B, C}, bytes());
        BOOST_CHECK(matcher.matches(params, log));
        // [A, B, E]
        log = protocol::LogEntry(address1, {A, B, E}, bytes());
        BOOST_CHECK(matcher.matches(params, log));

        // mismatched
        // [A]
        log = protocol::LogEntry(address1, {A}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [A, C]
        log = protocol::LogEntry(address1, {A, C}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [A, C, F]
        log = protocol::LogEntry(address1, {A, C, F}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [B]
        log = protocol::LogEntry(address1, {B}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [B, C]
        log = protocol::LogEntry(address1, {B, C}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
        // [B, C, F]
        log = protocol::LogEntry(address1, {B, C, F}, bytes());
        BOOST_CHECK(!matcher.matches(params, log));
    }
    // 5.[[A, B], [A, B]] "(A OR B) in first position AND (A OR B) in second position (and anything
    // after)"
    {
        auto params = std::make_shared<Web3FilterRequest>();
        params->resizeTopic(2);
        params->addTopic(0, toHexStringWithPrefix(A));
        params->addTopic(0, toHexStringWithPrefix(B));
        params->addTopic(1, toHexStringWithPrefix(C));
        params->addTopic(1, toHexStringWithPrefix(D));
        protocol::LogEntry log;

        // matched
        {
            // [A, C]
            log = protocol::LogEntry(address1, {A, C}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
            // [A, D]
            log = protocol::LogEntry(address1, {A, D}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
            // [B, C]
            log = protocol::LogEntry(address1, {B, C}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
            // [B, D]
            log = protocol::LogEntry(address1, {B, D}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
        }
        {
            // [A, C, E]
            log = protocol::LogEntry(address1, {A, C, E}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
            // [A, D, E]
            log = protocol::LogEntry(address1, {A, D, E}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
            // [B, C, E]
            log = protocol::LogEntry(address1, {B, C, E}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
            // [B, D, E]
            log = protocol::LogEntry(address1, {B, D, E}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
        }
        {
            // [A, C, F]
            log = protocol::LogEntry(address1, {A, C, F}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
            // [A, D, F]
            log = protocol::LogEntry(address1, {A, D, F}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
            // [B, C, F]
            log = protocol::LogEntry(address1, {B, C, F}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
            // [B, D, F]
            log = protocol::LogEntry(address1, {B, D, F}, bytes());
            BOOST_CHECK(matcher.matches(params, log));
        }
        // mismatched
        {
            // [A]
            log = protocol::LogEntry(address1, {A}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [B]
            log = protocol::LogEntry(address1, {B}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
        }
        {
            // [E, C]
            log = protocol::LogEntry(address1, {E, C}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [E, D]
            log = protocol::LogEntry(address1, {E, D}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [F, C]
            log = protocol::LogEntry(address1, {F, C}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [F, D]
            log = protocol::LogEntry(address1, {F, D}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
        }
        {
            // [A, E]
            log = protocol::LogEntry(address1, {A, E}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [A, F]
            log = protocol::LogEntry(address1, {A, F}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [B, E]
            log = protocol::LogEntry(address1, {B, E}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [B, F]
            log = protocol::LogEntry(address1, {B, F}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
        }
        {
            // [E, C, G]
            log = protocol::LogEntry(address1, {E, C, G}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [E, D, G]
            log = protocol::LogEntry(address1, {E, D, G}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [F, C, G]
            log = protocol::LogEntry(address1, {F, C, G}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [F, D, G]
            log = protocol::LogEntry(address1, {F, D, G}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
        }
        {
            // [A, E, G]
            log = protocol::LogEntry(address1, {A, E, G}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [A, F, G]
            log = protocol::LogEntry(address1, {A, F, G}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [B, E, G]
            log = protocol::LogEntry(address1, {B, E, G}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
            // [B, F, G]
            log = protocol::LogEntry(address1, {B, F, G}, bytes());
            BOOST_CHECK(!matcher.matches(params, log));
        }
    }
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test