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
 * @brief application for TxPoolService
 * @file TxPoolServiceApp.h
 * @author: yujiechen
 * @date 2021-10-17
 */
#pragma once
#include "Common/TarsUtils.h"
#include "TxPoolService/TxPoolServiceServer.h"
#include "libinitializer/TxPoolInitializer.h"
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <tarscpp/servant/Application.h>

namespace bcostars
{
class TxPoolServiceApp : public Application
{
public:
    TxPoolServiceApp() {}

    ~TxPoolServiceApp() override{};

    virtual void initialize() override;
    virtual void destroyApp() override {}

protected:
    virtual void initService();
    virtual void initConfig()
    {
        m_iniConfigPath = ServerConfig::BasePath + "/config.ini";
        m_privateKeyPath = ServerConfig::BasePath + "/node.pem";
        addConfig("node.pem");
        addConfig("config.ini");
    }

private:
    std::string m_iniConfigPath;
    std::string m_privateKeyPath;
    bcos::BoostLogInitializer::Ptr m_logInitializer;
    bcos::initializer::TxPoolInitializer::Ptr m_txpoolInitializer;
};
}  // namespace bcostars