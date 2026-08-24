/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#pragma once

#include <bcos-tool/Exceptions.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <sstream>
#include <string>

namespace bcos::test
{
// The per-section NodeConfig::loadXxx(ptree) loaders are protected; subclass to
// drive them in isolation, feeding each a hand-built ptree. This exercises the
// pure config-parsing branches (defaults, populated values, validation throws)
// without going through the all-or-nothing public loadConfig(). Shared by the
// NodeConfig*LoadersTest.cpp files.
struct LoaderProbe : public bcos::tool::NodeConfig
{
    using bcos::tool::NodeConfig::checkService;
    using bcos::tool::NodeConfig::getServiceName;
    using bcos::tool::NodeConfig::loadCertConfig;
    using bcos::tool::NodeConfig::loadChainConfig;
    using bcos::tool::NodeConfig::loadConsensusConfig;
    using bcos::tool::NodeConfig::loadExecutorConfig;
    using bcos::tool::NodeConfig::loadExecutorNormalConfig;
    using bcos::tool::NodeConfig::loadFailOverConfig;
    using bcos::tool::NodeConfig::loadGatewayConfig;
    using bcos::tool::NodeConfig::loadGenesisFeatures;
    using bcos::tool::NodeConfig::loadLedgerConfig;
    using bcos::tool::NodeConfig::loadOpEngineRpcConfig;
    using bcos::tool::NodeConfig::loadOthersConfig;
    using bcos::tool::NodeConfig::loadRpcConfig;
    using bcos::tool::NodeConfig::loadSealerConfig;
    using bcos::tool::NodeConfig::loadSecurityConfig;
    using bcos::tool::NodeConfig::loadSingleNodeConsensusConfig;
    using bcos::tool::NodeConfig::loadStorageConfig;
    using bcos::tool::NodeConfig::loadStorageSecurityConfig;
    using bcos::tool::NodeConfig::loadSyncConfig;
    using bcos::tool::NodeConfig::loadTxPoolConfig;
    using bcos::tool::NodeConfig::loadWeb3ChainConfig;
    using bcos::tool::NodeConfig::loadWeb3RpcConfig;
    using bcos::tool::NodeConfig::NodeConfig;
};

inline boost::property_tree::ptree fromIni(std::string const& ini)
{
    boost::property_tree::ptree pt;
    std::stringstream ss(ini);
    boost::property_tree::read_ini(ss, pt);
    return pt;
}
}  // namespace bcos::test
