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
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/sealer/SealerInterface.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <memory>

namespace bcos
{
namespace txpool
{
class TxPool;
}

namespace initializer
{
class TxPoolInitializer
{
public:
    using Ptr = std::shared_ptr<TxPoolInitializer>;
    TxPoolInitializer(bcos::tool::NodeConfig::Ptr _nodeConfig,
        ProtocolInitializer::Ptr _protocolInitializer,
        bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::ledger::LedgerInterface::Ptr _ledger, bool _preStoreTxs);
    virtual ~TxPoolInitializer() { stop(); }

    virtual void init(bcos::sealer::SealerInterface::Ptr _sealer);
    virtual void start();
    virtual void stop();

    std::shared_ptr<bcos::txpool::TxPoolInterface> txpool();
    bcos::crypto::CryptoSuite::Ptr cryptoSuite() { return m_protocolInitializer->cryptoSuite(); }

private:
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    ProtocolInitializer::Ptr m_protocolInitializer;
    bcos::front::FrontServiceInterface::Ptr m_frontService;
    bcos::ledger::LedgerInterface::Ptr m_ledger;

    std::shared_ptr<bcos::txpool::TxPool> m_txpool;
    std::atomic_bool m_running = {false};
};
}  // namespace initializer
}  // namespace bcos