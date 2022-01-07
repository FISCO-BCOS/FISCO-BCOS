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
 * @brief test for Gateway
 * @file FrontServiceBuilder.cpp
 * @author: octopus
 * @date 2021-05-21
 */
#pragma once

#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/interfaces/protocol/Protocol.h>
#include <bcos-front/FrontServiceFactory.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

inline std::shared_ptr<bcos::front::FrontService> buildFrontService(
    const std::string& _groupID, const std::string& _nodeID, const std::string& _configPath)
{
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto gatewayFactory = std::make_shared<bcos::gateway::GatewayFactory>("", "");
    auto frontServiceFactory = std::make_shared<bcos::front::FrontServiceFactory>();
    auto threadPool = std::make_shared<bcos::ThreadPool>("frontServiceTest", 16);

    // build gateway
    auto gateway = gatewayFactory->buildGateway(_configPath, true);

    // create nodeID by nodeID str
    auto nodeIDPtr =
        keyFactory->createKey(bcos::bytesConstRef((bcos::byte*)_nodeID.data(), _nodeID.size()));

    frontServiceFactory->setGatewayInterface(gateway);

    // create frontService
    auto frontService = frontServiceFactory->buildFrontService(_groupID, nodeIDPtr);
    // register front service to gateway
    gateway->gatewayNodeManager()->registerNode(_groupID, nodeIDPtr, frontService);
    // front service
    frontService->start();
    // start gateway
    gateway->start();

    return frontService;
}