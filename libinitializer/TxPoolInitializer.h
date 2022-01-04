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
 * @brief initializer for the TxPool module
 * @file TxPoolInitializer.h
 * @author: yujiechen
 * @date 2021-06-10
 */
#pragma once
#include "Common/TarsUtils.h"
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-framework/interfaces/ledger/LedgerInterface.h>
#include <bcos-framework/interfaces/sealer/SealerInterface.h>
#include <bcos-framework/libtool/NodeConfig.h>
#include <bcos-txpool/TxPool.h>
#include <bcos-txpool/TxPoolFactory.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>

namespace bcos
{
namespace initializer
{
class TxPoolInitializer
{
public:
    using Ptr = std::shared_ptr<TxPoolInitializer>;
    TxPoolInitializer(bcos::tool::NodeConfig::Ptr _nodeConfig,
        ProtocolInitializer::Ptr _protocolInitializer,
        bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::ledger::LedgerInterface::Ptr _ledger);
    virtual ~TxPoolInitializer() { stop(); }

    virtual void init(bcos::sealer::SealerInterface::Ptr _sealer);
    virtual void start();
    virtual void stop();

    bcos::txpool::TxPool::Ptr txpool() { return m_txpool; }
    bcos::crypto::CryptoSuite::Ptr cryptoSuite() { return m_protocolInitializer->cryptoSuite(); }

private:
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    ProtocolInitializer::Ptr m_protocolInitializer;
    bcos::front::FrontServiceInterface::Ptr m_frontService;
    bcos::ledger::LedgerInterface::Ptr m_ledger;

    bcos::txpool::TxPool::Ptr m_txpool;
    std::atomic_bool m_running = {false};
};
}  // namespace initializer
}  // namespace bcos