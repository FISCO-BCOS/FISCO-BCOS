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
 * @brief Initializer for all the modules
 * @file LocalInitializer.cpp
 * @author: yujiechen
 * @date 2021-10-28
 */
#include "AirNodeInitializer.h"
#include "libinitializer/Common.h"
#include "libinitializer/MemPoolInitializer.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-gateway/libamop/TopicManager.h>
#include <bcos-rpc/RpcFactory.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-rpc/tarsRPC/RPCServer.h>
#include <bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h>
#include <bcos-tool/Exceptions.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/atomic.hpp>

using namespace bcos::node;
using namespace bcos::initializer;
using namespace bcos::gateway;
using namespace bcos::rpc;
using namespace bcos::tool;

void AirNodeInitializer::init(std::string const& _configFilePath, std::string const& _genesisFile)
{
    bcos::protocol::g_BCOSConfig.setCodec(
        std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());

    boost::property_tree::ptree ptree;
    boost::property_tree::read_ini(_configFilePath, ptree);

    m_logInitializer = std::make_shared<BoostLogInitializer>();
    m_logInitializer->initLog(_configFilePath);
    INITIALIZER_LOG(INFO) << LOG_DESC("initGlobalConfig");

    // load nodeConfig
    // Note: this NodeConfig is used to create Gateway which not init the nodeName
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<NodeConfig>(keyFactory);
    nodeConfig->loadGenesisConfig(_genesisFile);
    nodeConfig->loadConfig(_configFilePath);
    // Cross-file EL-mode invariants (config.ini ethereum.mode=el must be backed by the
    // genesis EL declaration) — checkable only after BOTH files are loaded.
    nodeConfig->validateELModeInvariants();

    m_nodeInitializer = std::make_shared<bcos::initializer::Initializer>();
    m_nodeInitializer->initConfig(_configFilePath, _genesisFile, "", true);

    auto ioServicePool = std::make_shared<bcos::IOServicePool>(nodeConfig->ioThreadCount(), "io");
    m_nodeInitializer->setIOServicePool(ioServicePool);

    // create gateway
    // DataEncryption will be inited in ProtocolInitializer when storage_security.enable = true,
    // otherwise keyEncryption() will return nullptr
    GatewayFactory gatewayFactory(nodeConfig->chainId(), "localRpc",
        m_nodeInitializer->protocolInitializer()->getKeyEncryptionByType(
            nodeConfig->keyEncryptionType()));
    gatewayFactory.setIOServicePool(ioServicePool);
    auto gateway = gatewayFactory.buildGateway(_configFilePath, true, nullptr, "localGateway");
    m_gateway = gateway;

    // create the node
    m_nodeInitializer->init(bcos::protocol::NodeArchitectureType::AIR, _configFilePath,
        _genesisFile, m_gateway, true, m_logInitializer->logPath());

    auto pbftInitializer = m_nodeInitializer->pbftInitializer();
    auto groupInfo = m_nodeInitializer->pbftInitializer()->groupInfo();
    auto nodeService =
        std::make_shared<NodeService>(m_nodeInitializer->ledger(), m_nodeInitializer->scheduler(),
            m_nodeInitializer->txPoolInitializer()->txpool(), pbftInitializer->pbft(),
            pbftInitializer->blockSync(), m_nodeInitializer->protocolInitializer()->blockFactory(),
            m_nodeInitializer->engineService());
    // eth_getProof node reader (M8.3): committed MPT node rows straight from the state
    // backend. Set unconditionally — feature_mpt_state_root can activate at runtime, and a
    // pre-MPT header's stateRoot simply misses in the node rows (-32004); -32603 stays
    // reserved for nodes that structurally cannot serve proofs (tars-built NodeService).
    // Lifetime: the handle owns its adapter; the borrowed backend lives in m_nodeInitializer,
    // declared before m_rpc / m_tarsApplication (the NodeService holders), so it is
    // destroyed after them.
    nodeService->setMPTNodeReader(m_nodeInitializer->mptNodeReader());

    // eth_getStorageAt latest-state path: a provider that forks a fresh latest view of
    // GlobalStateStorage per request (see Initializer::stateStorageProvider for the lifetime
    // contract — the provider captures the GlobalStateStorageInitializer shared_ptr).
    nodeService->setStateStorageProvider(m_nodeInitializer->stateStorageProvider());

    // blockTag semantics ([web3_rpc] safe_block_depth / finalized_block_depth): how many
    // blocks behind "latest" the safe/finalized tags point to.
    nodeService->setSafeBlockDepth(nodeConfig->web3SafeBlockDepth());
    nodeService->setFinalizedBlockDepth(nodeConfig->web3FinalizedBlockDepth());

    // Engine-driven modes ([consensus] enable_single_node_consensus or [op_engine_rpc]):
    // route sendRawTransaction to the in-process mempool instead of txpool — the
    // EngineService seals these txs into blocks (driven by the built-in single-node timer
    // or by an external op-node), bypassing txpool/sealer/pbft, which are never initialized
    // in these modes.
    if (nodeConfig->engineDrivenBlockProduction() || nodeConfig->enableSingleNodeConsensus())
    {
        nodeService->setMemPool(m_nodeInitializer->memPoolInitializer()->memPool());
    }

    // create rpc
    RpcFactory rpcFactory(nodeConfig->chainId(), m_gateway, keyFactory,
        m_nodeInitializer->protocolInitializer()->getKeyEncryptionByType(
            nodeConfig->keyEncryptionType()));
    rpcFactory.setNodeConfig(nodeConfig);
    rpcFactory.setIOServicePool(ioServicePool);
    m_rpc = rpcFactory.buildLocalRpc(groupInfo, nodeService);
    if (gateway->amop())
    {
        gateway->amop()->topicManager()->setLocalClient(m_rpc);
    }
    m_nodeInitializer->initNotificationHandlers(m_rpc);

    // NOTE: this should be last called
    m_nodeInitializer->initSysContract();

    // tars rpc
    if (!nodeConfig->tarsRPCConfig().host.empty() && nodeConfig->tarsRPCConfig().port > 0 &&
        nodeConfig->ioThreadCount() > 0)
    {
        m_tarsApplication.emplace(nodeService);
        m_tarsConfig.emplace(RPCApplication::generateTarsConfig(nodeConfig->tarsRPCConfig().host,
            nodeConfig->tarsRPCConfig().port, nodeConfig->ioThreadCount()));
    }
}

void AirNodeInitializer::init(bcos::initializer::Params const& _params)
{
    // The config file is the source of truth for Ethereum L1 EL mode; the command-line
    // flags only mirror it and any conflict is a hard error (fail fast, never silently
    // prefer one side).
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<NodeConfig>(keyFactory);
    nodeConfig->loadGenesisConfig(_params.genesisFilePath);
    nodeConfig->loadConfig(_params.configFilePath);
    validateEthereumELParams(_params, *nodeConfig);

    init(_params.configFilePath, _params.genesisFilePath);

    // Ethereum L1 EL mode: after the core node is initialized, build the self-sync driver
    // over the SAME v2 scheduler + EthereumExecutor + global state storage the rest of the
    // v2 pipeline uses. It downloads blocks from bootnodes, verifies and commits them.
    if (nodeConfig->ethereumELModeEnabled())
    {
        auto initializer = m_nodeInitializer;
        if (!initializer->ethereumExecutor() || !initializer->ethereumSerialScheduler())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "Ethereum L1 EL mode requires executor_version >= 2 "
                                      "(the v2 EthereumExecutor); set [executor] version=2 "
                                      "in config.genesis"));
        }
        m_ethereumSync = std::make_shared<bcos::initializer::EthereumSyncInitializer>(nodeConfig,
            initializer->ledger(), initializer->protocolInitializer()->blockFactory(),
            initializer->ethereumSerialScheduler(), initializer->ethereumExecutor(),
            initializer->globalStateStorageInitializer(), initializer->ioServicePool());
        m_ethereumSync->validateConfig();
    }
}

void AirNodeInitializer::validateEthereumELParams(
    bcos::initializer::Params const& _params, bcos::tool::NodeConfig const& _nodeConfig)
{
    if (_params.ethereumEL.has_value())
    {
        bool const wantEL = *_params.ethereumEL;
        bool const configuredEL = _nodeConfig.ethereumELModeEnabled();
        if (wantEL && !configuredEL)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "command-line --el requests Ethereum L1 EL mode but "
                                      "[ethereum].mode != el in " +
                                      _params.configFilePath +
                                      "; the config file is the source of truth — either "
                                      "set [ethereum] mode=el or drop --el"));
        }
    }
    if (_params.ethereumBootnodesFile.has_value())
    {
        auto const& configured = _nodeConfig.ethereumBootnodesFile();
        // Normalise a leading "./" so `-b bootnodes.json` matches
        // bootnodes_file=./bootnodes.json (same file, two spellings).
        auto normalise = [](std::string const& p) {
            return p.rfind("./", 0) == 0 ? p.substr(2) : p;
        };
        if (normalise(*_params.ethereumBootnodesFile) != normalise(configured))
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "command-line --bootnodes '" +
                                      *_params.ethereumBootnodesFile +
                                      "' differs from [ethereum].bootnodes_file='" + configured +
                                      "' in " + _params.configFilePath +
                                      "; the config file is the source of truth — align them "
                                      "or drop --bootnodes"));
        }
    }
}

void AirNodeInitializer::start()
{
    if (m_nodeInitializer)
    {
        m_nodeInitializer->start();
    }

    // Ethereum L1 EL mode: the node is a pure Ethereum execution-layer client — the FISCO
    // gateway/P2P network is not part of the Ethereum stack, so it stays dormant. The
    // self-sync driver (bootnode download -> verify -> commit) is what moves the chain.
    const bool elMode = (m_ethereumSync != nullptr);
    if (m_gateway && !elMode)
    {
        m_gateway->start();
    }

    if (m_rpc)
    {
        m_rpc->start();
    }

    if (m_ethereumSync)
    {
        m_ethereumSync->start();
    }

    if (m_tarsApplication && m_tarsConfig)
    {
        boost::atomic_bool started = false;
        m_tarsThread.emplace([&, this]() {
            m_tarsApplication->main(*m_tarsConfig);
            started = true;
            started.notify_all();
            m_tarsApplication->waitForShutdown();
        });

        started.wait(false);
    }
}

void AirNodeInitializer::stop()
{
    try
    {
        if (m_ethereumSync)
        {
            m_ethereumSync->stop();
        }
        if (m_rpc)
        {
            m_rpc->stop();
        }
        if (m_gateway)
        {
            m_gateway->stop();
        }
        if (m_nodeInitializer)
        {
            m_nodeInitializer->stop();
        }
        if (m_tarsApplication && m_tarsThread)
        {
            m_tarsApplication->terminate();
            m_tarsThread->join();
        }
    }
    catch (std::exception const& e)
    {
        std::cout << "stop bcos-node failed for " << boost::diagnostic_information(e);
        exit(-1);
    }
}
