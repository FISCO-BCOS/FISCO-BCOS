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
 * @file TxPoolInitializer.cpp
 * @author: yujiechen
 * @date 2021-06-10
 */
#include "TxPoolInitializer.h"
#include "Common.h"
#include <bcos-txpool/TxPoolFactory.h>
#include <fisco-bcos-tars-service/Common/TarsUtils.h>

#include <utility>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::initializer;

TxPoolInitializer::TxPoolInitializer(bcos::tool::NodeConfig::Ptr _nodeConfig,
    ProtocolInitializer::Ptr _protocolInitializer,
    bcos::front::FrontServiceInterface::Ptr _frontService,
    bcos::ledger::LedgerInterface::Ptr _ledger)
  : m_nodeConfig(std::move(_nodeConfig)),
    m_protocolInitializer(std::move(_protocolInitializer)),
    m_frontService(std::move(_frontService)),
    m_ledger(std::move(_ledger))
{
    auto keyPair = m_protocolInitializer->keyPair();
    auto cryptoSuite = m_protocolInitializer->cryptoSuite();
    auto txpoolFactory = std::make_shared<TxPoolFactory>(keyPair->publicKey(), cryptoSuite,
        m_protocolInitializer->txResultFactory(), m_protocolInitializer->blockFactory(),
        m_frontService, m_ledger, m_nodeConfig->groupId(), m_nodeConfig->chainId(),
        m_nodeConfig->blockLimit(), m_nodeConfig->txpoolLimit());

    m_txpool = txpoolFactory->createTxPool(m_nodeConfig->notifyWorkerNum(),
        m_nodeConfig->verifierWorkerNum(), m_nodeConfig->txsExpirationTime());

    if (m_nodeConfig->enableSendTxByTree())
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("enableSendTxByTree");
        auto treeRouter =
            std::make_shared<tool::TreeTopology>(m_protocolInitializer->keyPair()->publicKey());
        m_txpool->setTreeRouter(std::move(treeRouter));
    }
}

void TxPoolInitializer::init(bcos::sealer::SealerInterface::Ptr _sealer)
{
    m_txpool->registerUnsealedTxsNotifier(
        [_sealer](size_t _unsealedTxsSize, std::function<void(Error::Ptr)> _onRecv) {
            try
            {
                _sealer->asyncNoteUnSealedTxsSize(_unsealedTxsSize, _onRecv);
            }
            catch (std::exception const& e)
            {
                INITIALIZER_LOG(WARNING)
                    << LOG_DESC("call UnsealedTxsNotifier to the sealer exception")
                    << LOG_KV("message", boost::diagnostic_information(e));
            }
        });
    m_txpool->init();
}

void TxPoolInitializer::start()
{
    if (m_running)
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("The txpool has already been started");
        return;
    }
    INITIALIZER_LOG(INFO) << LOG_DESC("Start the txpool");
    m_running = true;
    m_txpool->start();
}

void TxPoolInitializer::stop()
{
    if (!m_running)
    {
        INITIALIZER_LOG(INFO) << LOG_DESC("The txpool has already been stopped");
        return;
    }
    INITIALIZER_LOG(INFO) << LOG_DESC("Stop the txpool");
    m_running = false;
    m_txpool->stop();
}

bcos::txpool::TxPoolInterface::Ptr TxPoolInitializer::txpool()
{
    return m_txpool;
}
