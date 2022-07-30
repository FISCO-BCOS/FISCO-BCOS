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

#include <bcos-tars-protocol/client/LightNodeLedgerClientImpl.h>

#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-front/FrontServiceFactory.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-lightnode/ledger/LedgerImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <memory>

int main([[maybe_unused]] int argc, [[maybe_unused]] const char* argv[])
{
    std::string configFile = "config.ini";
    std::string genesisFile = "genesis.ini";

    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>(keyFactory);
    nodeConfig->loadConfig(configFile);
    nodeConfig->loadGenesisConfig(genesisFile);

    bcos::gateway::GatewayFactory gatewayFactory(nodeConfig->chainId(), "local", nullptr);
    auto gateway = gatewayFactory.buildGateway(configFile, true, nullptr, "localGateway");

    // Front
    auto front = std::make_shared<bcos::front::FrontService>();
    front->setMessageFactory(std::make_shared<bcos::front::FrontMessageFactory>());
    front->setGroupID(nodeConfig->groupId());
    front->setNodeID(keyFactory->createKey(nodeConfig->chainId()));
    front->setIoService(std::make_shared<boost::asio::io_service>());
    front->setGatewayInterface(std::move(gateway));
    front->setThreadPool(std::make_shared<bcos::ThreadPool>("p2p", 1));

    // Remote ledger
    bcos::ledger::LightNodeLedgerClientImpl remoteLedger(std::move(front), std::move(keyFactory));

    // local storage
    auto storage = StorageInitializer::build(
        storagePath, m_protocolInitializer->dataEncryption(), m_nodeConfig->keyPageSize());

    // Local ledger
    // bcos::ledger::LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher, >

    return 0;
}