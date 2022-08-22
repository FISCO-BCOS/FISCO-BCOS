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

 * @brief main for the fisco-bcos
 * @file main.cpp
 * @author: ancelmo
 * @date 2022-07-04
 */

#include <bcos-tars-protocol/impl/TarsHashable.h>

#include "RPCInitializer.h"
#include "libinitializer/CommandHelper.h"
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <libinitializer/ProtocolInitializer.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <memory>
#include <thread>

static auto newStorage(const std::string& path)
{
    boost::filesystem::create_directories(path);
    rocksdb::Options options;
    options.create_if_missing = true;
    options.compression = rocksdb::kZSTD;
    options.max_open_files = 512;

    // open DB
    rocksdb::DB* db = nullptr;
    rocksdb::Status s = rocksdb::DB::Open(options, path, &db);
    if (!s.ok())
    {
        BCOS_LOG(INFO) << LOG_DESC("open rocksDB failed") << LOG_KV("error", s.ToString());
        BOOST_THROW_EXCEPTION(std::runtime_error("open rocksDB failed, err:" + s.ToString()));
    }
    return std::make_shared<bcos::storage::RocksDBStorage>(
        std::unique_ptr<rocksdb::DB>(db), nullptr);
}

static auto startSyncerThread(bcos::concepts::ledger::Ledger auto fromLedger,
    bcos::concepts::ledger::Ledger auto toLedger,
    std::shared_ptr<bcos::boostssl::ws::WsService> wsService, std::string groupID,
    std::string nodeName, std::shared_ptr<std::atomic_bool> stopToken)
{
    std::thread worker([fromLedger = std::move(fromLedger), toLedger = std::move(toLedger),
                           wsService = std::move(wsService), groupID = std::move(groupID),
                           nodeName = std::move(nodeName),
                           stopToken = std::move(stopToken)]() mutable {
        while (!(*stopToken))
        {
            try
            {
                auto ledger = bcos::concepts::getRef(toLedger);

                auto beforeStatus = ledger.getStatus();
                ledger.template sync<std::remove_cvref_t<decltype(fromLedger)>, bcostars::Block>(
                    fromLedger, true);
                auto afterStatus = ledger.getStatus();

                // Notify the client if block number changed
                if (afterStatus.blockNumber > beforeStatus.blockNumber)
                {
                    auto sessions = wsService->sessions();
                    std::string group;
                    Json::Value response;
                    response["group"] = groupID;
                    response["nodeName"] = nodeName;
                    response["blockNumber"] = afterStatus.blockNumber;
                    auto resp = response.toStyledString();

                    for (auto& session : sessions)
                    {
                        if (session && session->isConnected())
                        {
                            auto message = wsService->messageFactory()->buildMessage();
                            message->setPacketType(bcos::protocol::MessageType::BLOCK_NOTIFY);
                            message->setPayload(
                                std::make_shared<bcos::bytes>(resp.begin(), resp.end()));
                            session->asyncSendMessage(message);
                        }
                    }
                }
            }
            catch (std::exception& e)
            {
                LIGHTNODE_LOG(INFO)
                    << "Sync block fail, may be connecting" << boost::diagnostic_information(e);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    });

    return worker;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] const char* argv[])
{
    auto param = bcos::initializer::initAirNodeCommandLine(argc, argv, false);
    bcos::initializer::showNodeVersionMetric();

    std::string configFile = param.configFilePath;
    std::string genesisFile = param.genesisFilePath;

    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(configFile, pt);

    auto logInitializer = std::make_shared<bcos::BoostLogInitializer>();
    logInitializer->initLog(pt);

    g_BCOSConfig.setCodec(std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());

    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>(keyFactory);
    nodeConfig->loadConfig(configFile);
    nodeConfig->loadGenesisConfig(genesisFile);

    auto protocolInitializer = bcos::initializer::ProtocolInitializer();
    protocolInitializer.init(nodeConfig);
    protocolInitializer.loadKeyPair(nodeConfig->privateKeyPath());
    auto nodeID = protocolInitializer.keyPair()->publicKey()->hex();

    auto front = std::make_shared<bcos::front::FrontService>();
    // gateway
    bcos::gateway::GatewayFactory gatewayFactory(nodeConfig->chainId(), "local", nullptr);
    auto gateway = gatewayFactory.buildGateway(configFile, true, nullptr, "localGateway");
    auto protocolInfo = g_BCOSConfig.protocolInfo(bcos::protocol::ProtocolModuleID::GatewayService);
    gateway->gatewayNodeManager()->registerNode(nodeConfig->groupId(),
        protocolInitializer.keyPair()->publicKey(), bcos::protocol::OBSERVER_NODE, front,
        protocolInfo);
    gateway->start();

    // front
    front->setMessageFactory(std::make_shared<bcos::front::FrontMessageFactory>());
    front->setGroupID(nodeConfig->groupId());
    front->setNodeID(protocolInitializer.keyPair()->publicKey());
    front->setIoService(std::make_shared<boost::asio::io_service>());
    front->setGatewayInterface(gateway);
    front->setThreadPool(std::make_shared<bcos::ThreadPool>("p2p", 1));
    front->registerModuleMessageDispatcher(bcos::protocol::BlockSync,
        [](bcos::crypto::NodeIDPtr, const std::string&, bcos::bytesConstRef) {});
    front->registerModuleMessageDispatcher(bcos::protocol::AMOP,
        [](bcos::crypto::NodeIDPtr, const std::string&, bcos::bytesConstRef) {});
    front->start();

    // clients
    auto p2pClient = std::make_shared<bcos::p2p::P2PClientImpl>(front, gateway, keyFactory);
    auto remoteLedger = std::make_shared<bcos::ledger::LedgerClientImpl>(p2pClient);
    auto remoteTransactionPool =
        std::make_shared<bcos::transaction_pool::TransactionPoolClientImpl>(p2pClient);
    auto transactionPool =
        std::make_shared<bcos::transaction_pool::TransactionPoolClientImpl>(p2pClient);
    auto scheduler = std::make_shared<bcos::scheduler::SchedulerClientImpl>(p2pClient);

    // local ledger
    auto storage = newStorage(nodeConfig->storagePath());
    bcos::storage::StorageImpl storageWrapper(std::move(storage));
    auto localLedger = std::make_shared<bcos::ledger::LedgerImpl<
        bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher, decltype(storageWrapper)>>(
        std::move(storageWrapper));

    // Prepare genesis block
    bcostars::Block genesisBlock;
    genesisBlock.blockHeader.data.blockNumber = 0;
    bcos::concepts::bytebuffer::assignTo(
        nodeConfig->genesisData(), genesisBlock.blockHeader.data.extraData);
    localLedger->setupGenesisBlock(std::move(genesisBlock));

    // rpc
    auto wsService = bcos::lightnode::initRPC(nodeConfig, nodeID, gateway, keyFactory, localLedger,
        remoteLedger, transactionPool, scheduler);
    wsService->start();

    auto stopToken = std::make_shared<std::atomic_bool>(false);
    auto syncer = startSyncerThread(remoteLedger, localLedger, wsService, nodeConfig->groupId(),
        nodeConfig->nodeName(), stopToken);
    syncer.join();

    return 0;
}