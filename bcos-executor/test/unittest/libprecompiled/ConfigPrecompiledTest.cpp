/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file ConfigPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-06-22
 */

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/ConsensusPrecompiled.h"
#include "precompiled/SystemConfigPrecompiled.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

namespace bcos::test
{
class ConfigPrecompiledFixture : public PrecompiledFixture
{
public:
    ConfigPrecompiledFixture()
    {
        codec = std::make_shared<CodecWrapper>(hashImpl, false);
        auto keyPageIgnoreTables = std::make_shared<std::set<std::string, std::less<>>>(
            IGNORED_ARRAY_310.begin(), IGNORED_ARRAY_310.end());
        setIsWasm(false, false, true, DEFAULT_VERSION);
        std::stringstream nodeFactory;
        nodeFactory << std::setfill('1') << std::setw(128) << 1;
        node1 = nodeFactory.str();
        std::stringstream().swap(nodeFactory);

        nodeFactory << std::setfill('2') << std::setw(128) << 2;
        node2 = nodeFactory.str();
        std::stringstream().swap(nodeFactory);

        nodeFactory << std::setfill('3') << std::setw(128) << 3;
        node3 = nodeFactory.str();
        std::stringstream().swap(nodeFactory);

        nodeFactory << std::setfill('4') << std::setw(128) << 4;
        node4 = nodeFactory.str();
        std::stringstream().swap(nodeFactory);
    }

    ~ConfigPrecompiledFixture() override = default;

    ExecutionMessage::UniquePtr rotate(protocol::BlockNumber _number, bcos::bytes const& pk,
        bcos::bytes const& msg, bcos::bytes const& proof, std::string const& txSender = "",
        int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig(precompiled::WSM_METHOD_ROTATE_STR, pk, msg, proof);
        auto tx = fakeTransaction(
            cryptoSuite, keyPair, "", in, std::to_string(utcSteadyTimeUs()), 100001, "1", "1");
        if (!txSender.empty())
        {
            tx->forceSender(Address(txSender).asBytes());
        }
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_number);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::CONSENSUS_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result;
    };

    ExecutionMessage::UniquePtr addSealer(protocol::BlockNumber _number, std::string const& node,
        u256 weight, std::string const& txSender = "", int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("addSealer(string,uint256)", node, weight);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        if (!txSender.empty())
        {
            tx->forceSender(Address(txSender).asBytes());
        }
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_number);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::CONSENSUS_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result;
    };

    ExecutionMessage::UniquePtr addObserver(protocol::BlockNumber _number, std::string const& node,
        std::string const& txSender = "", int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("addObserver(string)", node);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        if (!txSender.empty())
        {
            tx->forceSender(Address(txSender).asBytes());
        }
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_number);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::CONSENSUS_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result;
    };

    ExecutionMessage::UniquePtr setValueByKey(protocol::BlockNumber _number, std::string const& key,
        std::string const& value, std::string const& txSender = "", int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("setValueByKey(string,string)", key, value);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        if (!txSender.empty())
        {
            tx->forceSender(Address(txSender).asBytes());
        }
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_number);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::SYS_CONFIG_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result;
    };

    std::string getValueByKey(protocol::BlockNumber _number, std::string const& key)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("getValueByKey(string)", key);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params1 = std::make_unique<NativeExecutionMessage>();
        params1->setTransactionHash(hash);
        params1->setContextID(_number);
        params1->setSeq(1000);
        params1->setDepth(0);
        params1->setFrom(sender);
        params1->setTo(precompiled::SYS_CONFIG_ADDRESS);
        params1->setOrigin(sender);
        params1->setStaticCall(false);
        params1->setGasAvailable(gas);
        params1->setData(std::move(in));
        params1->setType(NativeExecutionMessage::TXHASH);

        /// call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(std::move(params1),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();

        std::string value;
        codec->decode(result->data(), value);

        commitBlock(_number);
        return value;
    };


    static std::tuple<bytes, bytes> generateVRFProof(
        crypto::KeyPairInterface::UniquePtr const& keyPair, HashType const& blockHash)
    {
        CInputBuffer privateKey{reinterpret_cast<const char*>(keyPair->secretKey()->data().data()),
            keyPair->secretKey()->size()};
        bytes vrfPublicKey;
        vrfPublicKey.resize(32);
        COutputBuffer publicKey{(char*)vrfPublicKey.data(), vrfPublicKey.size()};
        auto ret = wedpr_curve25519_vrf_derive_public_key(&privateKey, &publicKey);
        BOOST_CHECK_EQUAL(ret, WEDPR_SUCCESS);

        CInputBuffer inputMsg{reinterpret_cast<const char*>(blockHash.data()), blockHash.size()};
        bytes vrfProof;
        size_t proofSize = 96;
        vrfProof.resize(proofSize);
        COutputBuffer proof{(char*)vrfProof.data(), proofSize};
        ret = wedpr_curve25519_vrf_prove_utf8(&privateKey, &inputMsg, &proof);
        BOOST_CHECK(ret == WEDPR_SUCCESS);

        return {vrfPublicKey, vrfProof};
    }

    ledger::ConsensusNodeList getNodeList()
    {
        std::promise<storage::Entry> p;
        storage->asyncGetRow(ledger::SYS_CONSENSUS, "key", [&p](auto&& error, auto&& entry) {
            BOOST_CHECK(entry.has_value());
            p.set_value(entry.value());
        });
        auto entry = p.get_future().get();
        auto nodeList = entry.getObject<ledger::ConsensusNodeList>();
        return nodeList;
    }

    SystemConfigEntry getSystemConfigByKey(std::string_view _key)
    {
        std::promise<storage::Entry> p;
        storage->asyncGetRow(ledger::SYS_CONFIG, _key, [&p](auto&& error, auto&& entry) {
            BOOST_CHECK(entry.has_value());
            p.set_value(entry.value());
        });
        auto entry = p.get_future().get();
        auto systemConfig = entry.getObject<SystemConfigEntry>();
        return systemConfig;
    }

    std::string sender;
    std::string node1;
    std::string node2;
    std::string node3;
    std::string node4;
    Address contractAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
};
BOOST_FIXTURE_TEST_SUITE(precompiledConfigTest, ConfigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(sysConfig_test)
{
    auto simpleSetFunc = [this](protocol::BlockNumber _number, int _contextId,
                             const std::string& _key, const std::string& _v,
                             bcos::protocol::TransactionStatus _errorCode =
                                 bcos::protocol::TransactionStatus::None,
                             BlockVersion version = protocol::BlockVersion::MAX_VERSION) {
        nextBlock(_number, version);
        bytes in = codec->encodeWithSig("setValueByKey(string,string)", _key, _v);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(_contextId);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::SYS_CONFIG_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        BOOST_CHECK(result2->status() == (int32_t)_errorCode);
        commitBlock(_number);
    };
    bcos::protocol::BlockNumber number = 2;
    // simple set SYSTEM_KEY_TX_GAS_LIMIT
    {
        simpleSetFunc(
            number++, 100, std::string{ledger::SYSTEM_KEY_TX_GAS_LIMIT}, std::string("1000000"));
    }

    // simple get SYSTEM_KEY_TX_GAS_LIMIT
    {
        nextBlock(number++);
        bytes in = codec->encodeWithSig(
            "getValueByKey(string)", std::string(ledger::SYSTEM_KEY_TX_GAS_LIMIT));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(101);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::SYS_CONFIG_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        BOOST_CHECK(result2->data().toBytes() == codec->encode(std::string("1000000"), u256(3)));
        commitBlock(3);
    }

    // simple set SYSTEM_KEY_TX_COUNT_LIMIT
    {
        simpleSetFunc(
            number++, 102, std::string{ledger::SYSTEM_KEY_TX_COUNT_LIMIT}, std::string("1000"));
    }

    // set SYSTEM_KEY_TX_COUNT_LIMIT error
    {
        simpleSetFunc(number++, 103, std::string{ledger::SYSTEM_KEY_TX_COUNT_LIMIT},
            std::string("error"), bcos::protocol::TransactionStatus::PrecompiledError);
    }
    // set error key
    {
        simpleSetFunc(number++, 106, std::string("errorKey"), std::string("1000"),
            bcos::protocol::TransactionStatus::PrecompiledError);
    }

    // get error key
    {
        nextBlock(number++);
        bytes in = codec->encodeWithSig("getValueByKey(string)", std::string("error"));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(107);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::SYS_CONFIG_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        BOOST_CHECK(result2->data().toBytes() == codec->encode(std::string(""), s256(-1)));
        commitBlock(9);
    }

    // auth_check_status
    {
        // set auth check status, but version is not 3.3
        simpleSetFunc(number++, 106, std::string(ledger::SYSTEM_KEY_AUTH_CHECK_STATUS),
            std::string("1000"), bcos::protocol::TransactionStatus::PrecompiledError,
            BlockVersion::V3_2_VERSION);
        // success
        simpleSetFunc(
            number++, 106, std::string(ledger::SYSTEM_KEY_AUTH_CHECK_STATUS), std::string("1"));
    }

    // simple set SYSTEM_KEY_TX_GAS_PRICE
    {
        simpleSetFunc(
            number++, 108, std::string{ledger::SYSTEM_KEY_TX_GAS_PRICE}, std::string("0xa"));
    }
    // simple get SYSTEM_KEY_TX_GAS_PRICE
    {
        std::string gasPriceStr =
            getValueByKey(number++, std::string(ledger::SYSTEM_KEY_TX_GAS_PRICE));

        BCOS_LOG(DEBUG) << LOG_BADGE("sysConfig_test") << LOG_KV("gasPriceStr", gasPriceStr);
        BOOST_CHECK(gasPriceStr == "0xa");
    }
    // set SYSTEM_KEY_TX_GAS_PRICE error
    {
        simpleSetFunc(number++, 110, std::string{ledger::SYSTEM_KEY_TX_GAS_PRICE},
            std::string("error"), bcos::protocol::TransactionStatus::PrecompiledError);
    }
}

BOOST_AUTO_TEST_CASE(consensus_test)
{
    int64_t number = 2;

    std::stringstream nodeFactory;
    nodeFactory << std::setfill('1') << std::setw(128) << 1;
    std::string node1 = nodeFactory.str();
    std::stringstream().swap(nodeFactory);

    nodeFactory << std::setfill('2') << std::setw(128) << 2;
    std::string node2 = nodeFactory.str();
    std::stringstream().swap(nodeFactory);

    nodeFactory << std::setfill('3') << std::setw(128) << 3;
    std::string node3 = nodeFactory.str();
    std::stringstream().swap(nodeFactory);

    std::string errorNode = node1.substr(0, 127) + "s";

    auto callFunc = [this](protocol::BlockNumber _number, const std::string& method,
                        const std::string& _nodeId, int _w = -1, int _errorCode = 0) {
        BCOS_LOG(DEBUG) << LOG_BADGE("consensus_test") << LOG_KV("method", method)
                        << LOG_KV("_nodeId", _nodeId) << LOG_KV("_w", _w)
                        << LOG_KV("_errorCode", _errorCode);
        nextBlock(_number);
        bytes in = _w < 0 ? codec->encodeWithSig(method, _nodeId) :
                            codec->encodeWithSig(method, _nodeId, u256(_w));
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::CONSENSUS_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(int32_t(_errorCode)));
            if (result2->data().toBytes() != codec->encode(int32_t(_errorCode)))
            {
                PRECOMPILED_LOG(TRACE) << "Mismatch result: " << toHex(result2->data().toBytes())
                                       << " expect: " << toHex(codec->encode(int32_t(_errorCode)));
            }
        }
        commitBlock(_number);
        return result2;
    };

    // node id too short
    {
        callFunc(
            number++, "addSealer(string,uint256)", std::string("111111"), 1, CODE_INVALID_NODE_ID);

        callFunc(number++, "addObserver(string)", std::string("111111"), -1, CODE_INVALID_NODE_ID);
        callFunc(number++, "remove(string)", std::string("111111"), -1, CODE_INVALID_NODE_ID);
        callFunc(
            number++, "setWeight(string,uint256)", std::string("111111"), 11, CODE_INVALID_NODE_ID);
    }

    // add sealer node1
    {
        callFunc(number++, "addObserver(string)", node1, -1, 0);
        callFunc(number++, "addSealer(string,uint256)", node1, 1, 0);
    }

    // add observer node2
    {
        callFunc(number++, "addObserver(string)", node2, -1, 0);
    }

    // set weigh to observer
    {
        auto r = callFunc(number++, "setWeight(string,uint256)", node2, 123, 0);
        BOOST_CHECK(r->status() == 15);
    }

    // add errorNode
    {
        callFunc(number++, "addObserver(string)", errorNode, -1, CODE_INVALID_NODE_ID);
    }

    // turn last sealer to observer
    {
        callFunc(number++, "addObserver(string)", node1, -1, CODE_LAST_SEALER);
    }

    // add sealer node2
    {
        callFunc(number++, "addSealer(string,uint256)", node2, 1, 0);
        // removeTest sealer node2
        callFunc(number++, "remove(string)", node2, -1, 0);
        // remove not exist sealer
        callFunc(number++, "remove(string)", node2, -1, CODE_NODE_NOT_EXIST);

        // add observer node2
        callFunc(number++, "addObserver(string)", node2, -1, 0);
        // add sealer again
        callFunc(number++, "addSealer(string,uint256)", node2, 1, 0);
        // add observer again
        callFunc(number++, "addObserver(string)", node2, -1, 0);
    }

    // removeTest last sealer
    {
        callFunc(number++, "remove(string)", node1, -1, CODE_LAST_SEALER);
    }

    // set an invalid weight(0) to node
    {
        callFunc(number++, "setWeight(string,uint256)", node1, 0, CODE_INVALID_WEIGHT);
    }

    // set a valid weight(2) to node1
    {
        callFunc(number++, "setWeight(string,uint256)", node1, 2);
    }

    // removeTest observer
    {
        callFunc(number++, "remove(string)", node2, -1, 0);
    }

    // removeTest observer not exist
    {
        callFunc(number++, "remove(string)", node2, -1, CODE_NODE_NOT_EXIST);
    }

    // set weigh to not exist node2
    {
        callFunc(number++, "setWeight(string,uint256)", node2, 123, CODE_NODE_NOT_EXIST);
    }

    // add node3 to sealer
    {
        callFunc(
            number++, "addSealer(string,uint256)", node3, 1, CODE_ADD_SEALER_SHOULD_IN_OBSERVER);
        callFunc(number++, "addObserver(string)", node3, -1, 0);
        callFunc(number++, "addSealer(string,uint256)", node3, 1, 0);
    }
}

BOOST_AUTO_TEST_CASE(rotateValidTest)
{
    auto blockNumber = 1;
    auto keyPair = cryptoSuite->signatureImpl()->generateKeyPair();
    boost::log::core::get()->set_logging_enabled(false);
    addObserver(blockNumber++, keyPair->publicKey()->hex());
    addObserver(blockNumber++, node2);
    addObserver(blockNumber++, node3);
    addObserver(blockNumber++, node4);
    addSealer(blockNumber++, keyPair->publicKey()->hex(), 1);
    addSealer(blockNumber++, node2, 1);
    addSealer(blockNumber++, node3, 1);
    addSealer(blockNumber++, node4, 1);
    setValueByKey(blockNumber++, std::string(ledger::SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM), "4");
    setValueByKey(blockNumber++, std::string(ledger::SYSTEM_KEY_RPBFT_SWITCH), "1");
    setValueByKey(blockNumber, std::string(ledger::SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM),
        std::to_string(blockNumber + 1));
    boost::log::core::get()->set_logging_enabled(true);
    blockNumber++;

    auto blockHash = h256(blockNumber - 1);
    auto [vrfPublicKey, vrfProof] = generateVRFProof(keyPair, blockHash);

    // case1: valid vrf generated by some sealer
    auto result = rotate(blockNumber++, vrfPublicKey, blockHash.asBytes(), vrfProof,
        covertPublicToHexAddress(keyPair->publicKey()));
    BOOST_CHECK(result->status() == 0);

    auto nodeList = getNodeList();
    BOOST_CHECK(nodeList.size() == 4);
    std::for_each(nodeList.begin(), nodeList.end(), [&](const ledger::ConsensusNode& node) {
        BOOST_CHECK(node.type == ledger::CONSENSUS_SEALER);
    });

    // case2: valid proof, but the origin is not exist in the workingSealers
    setValueByKey(blockNumber++, std::string(ledger::SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM), "1");
    setValueByKey(blockNumber++, std::string(ledger::SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM), "1");
    blockHash = h256(blockNumber - 1);
    result = rotate(blockNumber++, vrfPublicKey, blockHash.asBytes(), vrfProof);
    BOOST_CHECK(result->status() == (uint32_t)TransactionStatus::PrecompiledError);
    BOOST_CHECK(result->message() == "ConsensusPrecompiled call undefined function!");
    auto notifyRotate =
        getValueByKey(blockNumber++, std::string(ledger::INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE));
    // FIXME: here is a bug
    //    BOOST_CHECK(notifyRotate == "1");

    // case3: invalid input(must be lastest hash)
    result = rotate(blockNumber++, vrfPublicKey, blockHash.asBytes(), vrfProof,
        covertPublicToHexAddress(keyPair->publicKey()));
    BOOST_CHECK(result->status() == (uint32_t)TransactionStatus::PrecompiledError);
    BOOST_CHECK(result->message() == "Invalid VRFInput, must be the parentHash!");

    // case4: invalid public key(the origin is not one of the sealers)
    blockHash = h256(blockNumber - 1);
    result = rotate(blockNumber++, keyPair->publicKey()->data(), blockHash.asBytes(), vrfProof,
        covertPublicToHexAddress(keyPair->publicKey()));
    BOOST_CHECK(result->status() == (uint32_t)TransactionStatus::PrecompiledError);
    BOOST_CHECK(result->message() == "Invalid VRF Public Key!");

    // case5: invalid proof
    // vrfProof invalid now
    blockHash = h256(blockNumber - 1);
    result = rotate(blockNumber++, vrfPublicKey, blockHash.asBytes(), vrfProof,
        covertPublicToHexAddress(keyPair->publicKey()));
    BOOST_CHECK(result->status() == (uint32_t)TransactionStatus::PrecompiledError);
    BOOST_CHECK(result->message() == "Verify VRF proof failed!");

    // case6: valid proof now
    blockHash = h256(blockNumber - 1);
    std::tie(vrfPublicKey, vrfProof) = generateVRFProof(keyPair, blockHash);
    result = rotate(blockNumber++, vrfPublicKey, blockHash.asBytes(), vrfProof,
        covertPublicToHexAddress(keyPair->publicKey()));
    BOOST_CHECK(result->status() == 0);

    nodeList = getNodeList();
    BOOST_CHECK(nodeList.size() == 4);
    // only one node is working sealer
    uint16_t workingSealerCount = 0;
    uint16_t candidateSealerCount = 0;
    for (const auto& node : nodeList)
    {
        if (node.type == ledger::CONSENSUS_SEALER)
        {
            workingSealerCount++;
        }
        if (node.type == ledger::CONSENSUS_CANDIDATE_SEALER)
        {
            candidateSealerCount++;
        }
    }
    BOOST_CHECK(workingSealerCount == 1);
    BOOST_CHECK(candidateSealerCount == 3);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test