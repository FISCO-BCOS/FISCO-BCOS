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
 * @brief NodeService
 * @file NodeService.cpp
 * @author: yujiechen
 * @date 2021-10-11
 */
#include "NodeService.h"
#include "Common.h"
#include "bcos-tool/NodeConfig.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/protocol/ServiceDesc.h>
#include <bcos-tars-protocol/client/LedgerServiceClient.h>
#include <bcos-tars-protocol/client/PBFTServiceClient.h>
#include <bcos-tars-protocol/client/SchedulerServiceClient.h>
#include <bcos-tars-protocol/client/TxPoolServiceClient.h>
#include <bcos-utilities/BoostLog.h>
using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::crypto;
using namespace bcos::group;
using namespace bcos::protocol;

NodeService::Ptr NodeServiceFactory::buildNodeService(std::string const&, std::string const&,
    bcos::group::ChainNodeInfo::Ptr _nodeInfo, bcos::tool::NodeConfig::Ptr _nodeConfig)
{
    auto appName = _nodeInfo->nodeName();
    // create cryptoSuite
    auto const& type = _nodeInfo->nodeCryptoType();
    CryptoSuite::Ptr cryptoSuite = nullptr;
    if (type == NodeCryptoType::SM_NODE)
    {
        cryptoSuite = createSMCryptoSuite();
    }
    else
    {
        cryptoSuite = createCryptoSuite();
    }
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    cryptoSuite->setKeyFactory(keyFactory);

    auto blockFactory = createBlockFactory(cryptoSuite);

    auto ledgerClient = createServicePrx<bcostars::LedgerServiceClient, bcostars::LedgerServicePrx>(
        LEDGER, _nodeInfo, _nodeConfig, blockFactory);
    if (!ledgerClient.first)
    {
        return nullptr;
    }

    auto schedulerClient =
        createServicePrx<bcostars::SchedulerServiceClient, bcostars::SchedulerServicePrx>(
            SCHEDULER, _nodeInfo, _nodeConfig, cryptoSuite);
    if (!schedulerClient.first)
    {
        return nullptr;
    }

    // create txpool client
    auto txpoolClient = createServicePrx<bcostars::TxPoolServiceClient, bcostars::TxPoolServicePrx>(
        TXPOOL, _nodeInfo, _nodeConfig, cryptoSuite, blockFactory);
    if (!txpoolClient.first)
    {
        return nullptr;
    }

    // create consensus client
    auto consensusClient = createServicePrx<bcostars::PBFTServiceClient, bcostars::PBFTServicePrx>(
        CONSENSUS, _nodeInfo, _nodeConfig);
    if (!consensusClient.first)
    {
        return nullptr;
    }

    // create sync client
    auto syncClient = createServicePrx<bcostars::BlockSyncServiceClient, bcostars::PBFTServicePrx>(
        CONSENSUS, _nodeInfo, _nodeConfig);
    if (!syncClient.first)
    {
        return nullptr;
    }

    // Note: m_engineService is nullptr by default. In AIR nodes, it is wired via
    // AirNodeInitializer. In MAX/Tars nodes, the EngineService needs to be exposed as a
    // Tars servant before the RPC process can obtain a proxy to it (TODO: MAX wiring).
    auto nodeService = std::make_shared<NodeService>(ledgerClient.first, schedulerClient.first,
        txpoolClient.first, consensusClient.first, syncClient.first, blockFactory,
        m_engineService);

    nodeService->setLedgerPrx(ledgerClient.second);

    // blockTag semantics ([web3_rpc] safe_block_depth / finalized_block_depth): without this,
    // a MAX/pro deployment's NodeService keeps the default 0 and the operator's configured
    // depths would be parsed and logged by NodeConfig but silently ignored.
    nodeService->setSafeBlockDepth(_nodeConfig->web3SafeBlockDepth());
    nodeService->setFinalizedBlockDepth(_nodeConfig->web3FinalizedBlockDepth());

    // Finding J (round-2): a tars-built NodeService has NO local MPT node reader — only
    // AirNodeInitializer wires one. So any non-zero safe/finalized depth routes every
    // safe/finalized state read into the historical MPT path, which hard-errors with
    // -32603 "MPT not enabled on this node". That is a silent capability mismatch for an
    // operator who configures a depth here (the config is the same shared config.ini AIR
    // uses, where it works) — warn prominently at startup.
    if (_nodeConfig->web3SafeBlockDepth() > 0 || _nodeConfig->web3FinalizedBlockDepth() > 0)
    {
        BCOS_LOG(WARNING) << LOG_BADGE("NodeService") << LOG_DESC(
                                 "safe/finalized blockTag depth configured but this node has "
                                 "no MPT node reader: safe/finalized state reads will fail "
                                 "with -32603 'MPT not enabled on this node'. The "
                                 "safe_block_depth / finalized_block_depth historical path is "
                                 "only served by AIR nodes (which wire an MPT node reader). "
                                 "Set the depths to 0 to keep safe/finalized on the latest "
                                 "plane.")
                          << LOG_KV("safeBlockDepth", _nodeConfig->web3SafeBlockDepth())
                          << LOG_KV("finalizedBlockDepth", _nodeConfig->web3FinalizedBlockDepth());
    }

    return nodeService;
}