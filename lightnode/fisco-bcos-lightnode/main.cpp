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

#include "client/LedgerClientImpl.h"
#include "client/P2PClientImpl.h"
#include "client/SchedulerClientImpl.h"
#include "client/TransactionPoolClientImpl.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-front/FrontServiceFactory.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-lightnode/ledger/LedgerImpl.h>
#include <bcos-lightnode/rpc/LightNodeRPC.h>
#include <bcos-lightnode/storage/StorageImpl.h>
#include <bcos-rpc/Common.h>
#include <bcos-rpc/RpcFactory.h>
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <json/forwards.h>
#include <json/value.h>
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
    bcos::concepts::ledger::Ledger auto toLedger, std::atomic_bool const& stopToken)
{
    std::thread worker([fromLedger = std::move(fromLedger), toLedger = std::move(toLedger),
                           &stopToken]() mutable {
        while (!stopToken)
        {
            try
            {
                auto ledger = bcos::concepts::getRef(toLedger);
                ledger.template sync<std::remove_cvref_t<decltype(fromLedger)>, bcostars::Block>(
                    fromLedger, true);
            }
            catch (std::exception& e)
            {
                // std::cout << boost::diagnostic_information(e);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    });

    return worker;
}

static auto initRPC(bcos::tool::NodeConfig::Ptr nodeConfig, std::string nodeID,
    bcos::gateway::Gateway::Ptr gateway, bcos::crypto::KeyFactory::Ptr keyFactory,
    bcos::concepts::ledger::Ledger auto localLedger,
    bcos::concepts::ledger::Ledger auto remoteLedger,
    bcos::concepts::transacton_pool::TransactionPool auto transactionPool,
    bcos::concepts::scheduler::Scheduler auto scheduler)
{
    bcos::rpc::RpcFactory rpcFactory(nodeConfig->chainId(), gateway, keyFactory, nullptr);
    auto wsConfig = rpcFactory.initConfig(nodeConfig);
    auto wsService = rpcFactory.buildWsService(wsConfig);
    auto jsonrpc = std::make_shared<bcos::rpc::LightNodeRPC<decltype(localLedger),
        decltype(remoteLedger), decltype(transactionPool), decltype(scheduler),
        bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>>(localLedger, remoteLedger,
        transactionPool, scheduler, nodeConfig->chainId(), nodeConfig->groupId());
    wsService->registerMsgHandler(bcos::protocol::MessageType::HANDESHAKE,
        [nodeConfig, nodeID](std::shared_ptr<bcos::boostssl::MessageFace> msg,
            std::shared_ptr<bcos::boostssl::ws::WsSession> session) {
            RPC_LOG(INFO) << "LightNode handshake request";
            Json::Value handshakeResponse(Json::objectValue);
            Json::Value groupBlockNumberArray(Json::arrayValue);
            Json::Value group0(Json::objectValue);
            group0[nodeConfig->groupId()] = 0;
            groupBlockNumberArray.append(std::move(group0));

            // Generate genesis info
            Json::Value genesisConfig;
            genesisConfig["consensusType"] = nodeConfig->consensusType();
            genesisConfig["blockTxCountLimit"] = nodeConfig->ledgerConfig()->blockTxCountLimit();
            genesisConfig["txGasLimit"] = (int64_t)(nodeConfig->txGasLimit());
            genesisConfig["consensusLeaderPeriod"] =
                nodeConfig->ledgerConfig()->leaderSwitchPeriod();
            Json::Value sealerList(Json::arrayValue);
            auto consensusNodeList = nodeConfig->ledgerConfig()->consensusNodeList();
            for (auto const& node : consensusNodeList)
            {
                Json::Value sealer;
                sealer["nodeID"] = node->nodeID()->hex();
                sealer["weight"] = node->weight();
                sealerList.append(sealer);
            }
            genesisConfig["sealerList"] = sealerList;
            Json::FastWriter fastWriter;
            std::string genesisConfigStr = fastWriter.write(genesisConfig);
            // Generate genesis info end

            Json::Value groupInfoListArray(Json::arrayValue);
            Json::Value group0Info(Json::objectValue);
            group0Info["chainID"] = nodeConfig->chainId();
            group0Info["genesisConfig"] = genesisConfigStr;
            group0Info["groupID"] = nodeConfig->groupId();
            group0Info["iniConfig"] = "";

            Json::Value nodeList(Json::arrayValue);
            Json::Value node0(Json::objectValue);

            Json::Value iniConfig;
            iniConfig["isWasm"] = nodeConfig->isWasm();
            iniConfig["smCryptoType"] = nodeConfig->smCryptoType();
            iniConfig["chainID"] = nodeConfig->chainId();
            std::string iniStr = fastWriter.write(iniConfig);

            node0["iniConfig"] = iniStr;
            node0["microService"] = false;
            node0["name"] = nodeConfig->nodeName();
            node0["nodeID"] = nodeID;
            node0["protocol"]["compatibilityVersion"] = 4;
            node0["protocol"]["maxSupportedVersion"] = 1;
            node0["protocol"]["minSupportedVersion"] = 0;
            node0["serviceInfo"] = Json::Value(Json::arrayValue);
            node0["type"] = 0;
            nodeList.append(std::move(node0));

            group0Info["nodeList"] = std::move(nodeList);
            groupInfoListArray.append(std::move(group0Info));

            handshakeResponse["groupBlockNumber"] = std::move(groupBlockNumberArray);
            handshakeResponse["groupInfoList"] = std::move(groupInfoListArray);
            handshakeResponse["protocolVersion"] = 1;

            Json::FastWriter writer;
            auto response = writer.write(handshakeResponse);

            msg->setPayload(std::make_shared<bcos::bytes>(response.begin(), response.end()));
            session->asyncSendMessage(msg);
            RPC_LOG(INFO) << LOG_DESC("LightNode handshake success")
                          << LOG_KV("version", session->version())
                          << LOG_KV("endpoint", session ? session->endPoint() : "unknown")
                          << LOG_KV("handshakeResponse", response);
        });
    wsService->registerMsgHandler(bcos::rpc::AMOPClientMessageType::AMOP_SUBTOPIC,
        [](std::shared_ptr<bcos::boostssl::MessageFace> msg,
            std::shared_ptr<bcos::boostssl::ws::WsSession> session) {
            RPC_LOG(INFO) << "LightNode amop topic request";
        });
    wsService->registerMsgHandler(bcos::protocol::MessageType::RPC_REQUEST,
        [jsonrpc = std::move(jsonrpc)](std::shared_ptr<bcos::boostssl::MessageFace> msg,
            std::shared_ptr<bcos::boostssl::ws::WsSession> session) mutable {
            auto buffer = msg->payload();
            auto req = std::string_view((const char*)buffer->data(), buffer->size());

            jsonrpc->onRPCRequest(req, [m_buffer = std::move(buffer), msg = std::move(msg),
                                           session = std::move(session)](bcos::bytes resp) {
                if (session && session->isConnected())
                {
                    // TODO: no need to copy resp
                    auto buffer = std::make_shared<bcos::bytes>(std::move(resp));
                    msg->setPayload(buffer);
                    session->asyncSendMessage(msg);
                }
                else
                {
                    // remove the callback
                    RPC_LOG(WARNING)
                        << LOG_DESC("Unable to send response for session has been inactive")
                        << LOG_KV("req",
                               std::string_view((const char*)m_buffer->data(), m_buffer->size()))
                        << LOG_KV("resp", std::string_view((const char*)resp.data(), resp.size()))
                        << LOG_KV("seq", msg->seq())
                        << LOG_KV("endpoint", session ? session->endPoint() : std::string(""));
                }
            });
        });
    return wsService;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] const char* argv[])
{
    std::string configFile = "config.ini";
    std::string genesisFile = "config.genesis";

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


    // rpc
    auto wsService = initRPC(nodeConfig, nodeID, gateway, keyFactory, localLedger, remoteLedger,
        transactionPool, scheduler);
    wsService->start();

    auto stopToken = false;
    auto syncer = startSyncerThread(remoteLedger, localLedger, stopToken);
    syncer.join();

    return 0;
}