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
 * @file mini_consensus.cpp
 * @author: kyonGuo
 * @date 2023/2/28
 */

#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-utilities/BoostLogInitializer.h"
#include "fisco-bcos-air/Common.h"
#include "libinitializer/CommandHelper.h"
#include "libinitializer/Initializer.h"
#include "libinitializer/LedgerInitializer.h"
#include "libinitializer/StorageInitializer.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-task/Wait.h>
#include <execinfo.h>
#include <stdexcept>
#include <thread>

using namespace bcos;
using namespace bcos::initializer;
using namespace bcos::node;
using namespace bcos::gateway;
using namespace bcos::tool;
using namespace bcos::scheduler;

class MockScheduler : public SchedulerInterface
{
public:
    void executeBlock(bcos::protocol::Block::Ptr block, [[maybe_unused]] bool verify,
        std::function<void(bcos::Error::Ptr&&, bcos::protocol::BlockHeader::Ptr&&, bool)> callback)
        override
    {
        callback(nullptr, block->blockHeader(), false);
    }
    void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr&&, bcos::ledger::LedgerConfig::Ptr&&)> callback)
        override
    {
        auto ledgerConfig = std::make_shared<bcos::ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(header->number());
        ledgerConfig->setHash(header->hash());
        ledgerConfig->setBlockTxCountLimit(1000);
        ledgerConfig->setGasLimit({3000000000, 0});
        callback(nullptr, std::move(ledgerConfig));
    }
    void status(
        [[maybe_unused]] std::function<void(Error::Ptr&&, bcos::protocol::Session::ConstPtr&&)>
            callback) override
    {}
    void call([[maybe_unused]] protocol::Transaction::Ptr tx,
        [[maybe_unused]] std::function<void(Error::Ptr&&, protocol::TransactionReceipt::Ptr&&)>
            function) override
    {}

    void reset([[maybe_unused]] std::function<void(Error::Ptr&&)> callback) override {}
    void getCode([[maybe_unused]] std::string_view contract,
        std::function<void(Error::Ptr, bcos::bytes)> callback) override
    {}
    void getABI([[maybe_unused]] std::string_view contract,
        std::function<void(Error::Ptr, std::string)> callback) override
    {}
    void preExecuteBlock([[maybe_unused]] bcos::protocol::Block::Ptr block,
        [[maybe_unused]] bool verify, std::function<void(Error::Ptr&&)> callback) override
    {
        callback(nullptr);
    }
};

void createTxs(bcos::initializer::Initializer::Ptr const& init)
{
    bytes input = {0};
    auto tx = init->protocolInitializer()->blockFactory()->transactionFactory()->createTransaction(
        0, precompiled::AUTH_COMMITTEE_ADDRESS, input, "", 1000, init->nodeConfig()->chainId(),
        init->nodeConfig()->groupId(), utcSteadyTime());
    auto sender = Address(precompiled::AUTH_COMMITTEE_ADDRESS);
    tx->forceSender(sender.asBytes());
    while (true)
    {
        tx->setNonce(std::to_string(utcSteadyTime()));
        task::wait([](bcos::initializer::Initializer::Ptr const& init,
                       decltype(tx) tx) -> task::Task<void> {
            co_await init->txPoolInitializer()->txpool()->submitTransaction(tx);
        }(init, tx));
    }
}

void initAndStart(std::string const& _configFilePath, std::string const& _genesisFile)
{
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_configFilePath, pt);
    auto logInitializer = std::make_shared<BoostLogInitializer>();
    logInitializer->initLog(_configFilePath);
    INITIALIZER_LOG(INFO) << LOG_DESC("initGlobalConfig");

    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<NodeConfig>(keyFactory);
    nodeConfig->loadGenesisConfig(_genesisFile);
    nodeConfig->loadConfig(_configFilePath);

    auto nodeInitializer = std::make_shared<bcos::initializer::Initializer>();
    nodeInitializer->initConfig(_configFilePath, _genesisFile, "", true);

    GatewayFactory gatewayFactory(nodeConfig->chainId(), "localRpc",
        nodeInitializer->protocolInitializer()->dataEncryption());
    auto gateway = gatewayFactory.buildGateway(_configFilePath, true, nullptr, "localGateway");

    auto frontServiceInitializer = std::make_shared<FrontServiceInitializer>(
        nodeInitializer->nodeConfig(), nodeInitializer->protocolInitializer(), gateway);

    auto transactionSubmitResultFactory =
        std::make_shared<protocol::TransactionSubmitResultFactoryImpl>();

    auto storage = StorageInitializer::build(nodeConfig->storagePath(),
        nodeInitializer->protocolInitializer()->dataEncryption(), nodeConfig->keyPageSize());
    auto ledger = LedgerInitializer::build(
        nodeInitializer->protocolInitializer()->blockFactory(), storage, nodeConfig);

    // init the txpool
    auto txpoolInitializer = std::make_shared<TxPoolInitializer>(nodeConfig,
        nodeInitializer->protocolInitializer(), frontServiceInitializer->front(), ledger);

    auto nodeTimeMaintenance = std::make_shared<NodeTimeMaintenance>();
    auto consensusStoragePath = nodeConfig->storagePath() + "/consensus_log";
    auto consensusStorage = StorageInitializer::build(
        consensusStoragePath, nodeInitializer->protocolInitializer()->dataEncryption());
    auto scheduler = std::make_shared<MockScheduler>();
    auto pbftInitializer = std::make_shared<PBFTInitializer>(protocol::NodeArchitectureType::AIR,
        nodeConfig, nodeInitializer->protocolInitializer(), txpoolInitializer->txpool(), ledger,
        scheduler, consensusStorage, frontServiceInitializer->front(), nodeTimeMaintenance);

    txpoolInitializer->init(pbftInitializer->sealer());
    pbftInitializer->init();
    frontServiceInitializer->init(
        pbftInitializer->pbft(), pbftInitializer->blockSync(), txpoolInitializer->txpool());
    nodeInitializer->start();
    gateway->start();
    createTxs(nodeInitializer);
}

int main(int argc, const char* argv[])
{
    ExitHandler exitHandler;
    signal(SIGTERM, &ExitHandler::exitHandler);
    signal(SIGABRT, &ExitHandler::exitHandler);
    signal(SIGINT, &ExitHandler::exitHandler);

    // Note: the initializer must exist in the lifetime of the whole program
    try
    {
        auto param = bcos::initializer::initAirNodeCommandLine(argc, argv, false);
        bcos::initializer::showNodeVersionMetric();
        initAndStart(param.configFilePath, param.genesisFilePath);
    }
    catch (std::exception const& e)
    {
        bcos::initializer::printVersion();
        std::cout << "[" << bcos::getCurrentDateTime() << "] ";
        std::cout << "start fisco-bcos failed, error:" << boost::diagnostic_information(e)
                  << std::endl;
        return -1;
    }
    bcos::initializer::printVersion();
    std::cout << "[" << bcos::getCurrentDateTime() << "] ";
    std::cout << "The fisco-bcos is running..." << std::endl;
    while (!exitHandler.shouldExit())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::cout << "[" << bcos::getCurrentDateTime() << "] ";
    std::cout << "fisco-bcos program exit normally." << std::endl;
}