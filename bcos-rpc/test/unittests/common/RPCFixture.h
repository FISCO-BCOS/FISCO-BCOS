/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file RPCFixture.h
 * @author: kyonGuo
 * @date 2023/7/20
 */

#pragma once

#include <bcos-crypto/encrypt/AESCrypto.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-crypto/signature/sm2/SM2KeyPair.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/testutils/faker/FakeFrontService.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-framework/testutils/faker/FakeScheduler.h>
#include <bcos-framework/testutils/faker/FakeSealer.h>
#include <bcos-framework/testutils/faker/FakeTxPool.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-rpc/RpcFactory.h>
#include <bcos-rpc/tarsRPC/RPCServer.h>
#include <bcos-rpc/validator/CallValidator.h>
#include <bcos-txpool/TxPoolConfig.h>
#include <bcos-txpool/TxPoolFactory.h>
#include <bcos-txpool/sync/TransactionSync.h>
#include <bcos-txpool/txpool/storage/MemoryStorage.h>
#include <bcos-txpool/txpool/validator/TxValidator.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::crypto;
namespace bcos::test
{
class RPCFixture : public TestPromptFixture
{
public:
    RPCFixture() { init(); }
    ~RPCFixture()
    {
        if (m_ledger)
        {
            m_ledger->stop();
        }
        if (txPool)
        {
            txPool->stop();
        }
    };
    void init(bool isSm = false)
    {
        hashImpl = std::make_shared<Keccak256>();
        sign = std::make_shared<Secp256k1Crypto>();
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, sign, nullptr);
        gateway = std::make_shared<FakeGateWayWrapper>();
        factory =
            std::make_shared<bcos::rpc::RpcFactory>(chainId, gateway, cryptoSuite->keyFactory());
        nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
        nodeConfig->loadConfigFromString(configini);
        factory->setNodeConfig(nodeConfig);

        auto blockHeaderFactory =
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
        auto txFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
        auto receiptFactory =
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
        m_blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, txFactory, receiptFactory);
        m_ledger = std::make_shared<FakeLedger>(m_blockFactory, 20, 10, 10);
        m_ledger->setSystemConfig(ledger::SYSTEM_KEY_TX_COUNT_LIMIT, "1000");

        auto nodeId = std::make_shared<KeyImpl>(
            h256("1110000000000000000000000000000000000000000000000000000000000000").asBytes());
        m_frontService = std::make_shared<FakeFrontService>(nodeId);
        auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();

        auto txPoolFactory = std::make_shared<TxPoolFactory>(nodeId, cryptoSuite, txResultFactory,
            m_blockFactory, m_frontService, m_ledger, "group0", "chain0", 100000000);

        txPool = txPoolFactory->createTxPool();
        txPool->init();
        txPool->start();
        scheduler = std::make_shared<FakeScheduler>(m_ledger, m_blockFactory);

        nodeService = std::make_shared<rpc::NodeService>(
            m_ledger, scheduler, txPool, nullptr, nullptr, m_blockFactory);


        groupInfo = std::make_shared<group::GroupInfo>();
        auto chainNode = std::make_shared<group::ChainNodeInfo>();
        auto service = group::ChainNodeInfo::ServicesInfo();
        service.insert({bcos::protocol::ServiceType::RPC, "rpc"});
        chainNode->setNodeID("node1");
        chainNode->setServicesInfo(std::move(service));
        groupInfo->updateNodeInfo(chainNode);
        groupInfo->setGroupID(groupId);
        groupInfo->setChainID(chainId);
    }
    bcos::tool::NodeConfig::Ptr nodeConfig;
    RPCInterface::Ptr rpc;
    Hash::Ptr hashImpl;
    SignatureCrypto::Ptr sign;
    CryptoSuite::Ptr cryptoSuite;
    FakeGateWayWrapper::Ptr gateway;
    std::string groupId = "test-group";
    std::string chainId = "test-chain";
    RpcFactory::Ptr factory;

    FakeFrontService::Ptr m_frontService;
    FakeLedger::Ptr m_ledger;
    TxPool::Ptr txPool;
    FakeScheduler::Ptr scheduler;
    BlockFactory::Ptr m_blockFactory;

    rpc::NodeService::Ptr nodeService;
    group::GroupInfo::Ptr groupInfo;
    // clang-format off
    std::string configini = "[p2p]\n"
        "    listen_ip=0.0.0.0\n"
        "    listen_port=30300\n"
        "    ; ssl or sm ssl\n"
        "    sm_ssl=false\n"
        "    nodes_path=./\n"
        "    nodes_file=nodes.json\n"
        "    ; enable rip protocol, default: true\n"
        "    ; enable_rip_protocol=false\n"
        "    ; enable compression for p2p message, default: true\n"
        "    ; enable_compression=false\n"
        "\n"
        "[web3_rpc]\n"
        "    enable=true\n"
        "    listen_ip=127.0.0.1\n"
        "    listen_port=8555\n"
        "\n"
        "[rpc]\n"
        "    listen_ip=0.0.0.0\n"
        "    listen_port=20200\n"
        "    thread_count=4\n"
        "    ; ssl or sm ssl\n"
        "    sm_ssl=false\n"
        "    ; ssl connection switch, if disable the ssl connection, default: false\n"
        "    disable_ssl=true\n"
        "    ; return input params in sendTransaction() return, default: true\n"
        "    ; return_input_params=false\n"
        "\n"
        "[cert]\n"
        "    ; directory the certificates located in\n"
        "    ca_path=./conf\n"
        "    ; the ca certificate file\n"
        "    ca_cert=ca.crt\n"
        "    ; the node private key file\n"
        "    node_key=ssl.key\n"
        "    ; the node certificate file\n"
        "    node_cert=ssl.crt\n"
        "    ; directory the multiple certificates located in\n"
        "    multi_ca_path=multiCaPath\n"
        "\n";
std::string wrongConfigini = "[p2p]\n"
    "    listen_ip=0.0.0.0\n"
    "    listen_port=30300\n"
    "    ; ssl or sm ssl\n"
    "    sm_ssl=false\n"
    "    nodes_path=./\n"
    "    nodes_file=nodes.json\n"
    "    ; enable rip protocol, default: true\n"
    "    ; enable_rip_protocol=false\n"
    "    ; enable compression for p2p message, default: true\n"
    "    ; enable_compression=false\n"
    "\n"
    "[rpc]\n"
    "    listen_ip=0.0.1243.0\n"
    "    listen_port=2022200\n"
    "    thread_count=4\n"
    "    ; ssl or sm ssl\n"
    "    sm_ssl=false\n"
    "    ; ssl connection switch, if disable the ssl connection, default: false\n"
    "    disable_ssl=true\n"
    "    ; return input params in sendTransaction() return, default: true\n"
    "    ; return_input_params=false\n"
    "\n"
    "[cert]\n"
    "    ; directory the certificates located in\n"
    "    ca_path=./conf\n"
    "    ; the ca certificate file\n"
    "    ca_cert=ca.crt\n"
    "    ; the node private key file\n"
    "    node_key=ssl.key\n"
    "    ; the node certificate file\n"
    "    node_cert=ssl.crt\n"
    "    ; directory the multiple certificates located in\n"
    "    multi_ca_path=multiCaPath\n"
    "\n";
    // clang-format on
};
}  // namespace bcos::test
