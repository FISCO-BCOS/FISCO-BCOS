/*
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
 * @brief Cache uncommitted but executed block info
 * @file LedgerCache.h
 * @author: jimmyshi
 * @date: 2022-11-07
 */

#pragma once

#include "../Common.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-ledger/LedgerMethods.h"
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-task/Wait.h>
#include <future>
#include <optional>

namespace bcos::executor
{
class LedgerCache
{
private:
    ledger::LedgerInterface::Ptr m_ledger;
    ledger::LedgerConfig::Ptr m_ledgerConfig;

    std::map<int64_t, h256, std::less<>> m_blockNumber2Hash;
    mutable bcos::SharedMutex x_blockNumber2Hash;

public:
    using Ptr = std::shared_ptr<LedgerCache>;
    LedgerCache(bcos::ledger::LedgerInterface::Ptr ledger)
      : m_ledger(std::move(ledger)), m_ledgerConfig(std::make_shared<ledger::LedgerConfig>())
    {}

    const ledger::LedgerConfig& ledgerConfig() const { return *m_ledgerConfig; }
    void updateLedgerConfig()
    {
        task::syncWait(ledger::getLedgerConfig(*m_ledger, *m_ledgerConfig));
    }

    void setBlockNumber2Hash(int64_t blockNumber, h256 blockHash)
    {
        bcos::WriteGuard l(x_blockNumber2Hash);
        m_blockNumber2Hash[blockNumber] = blockHash;
    }

    void clearCacheByNumber(int64_t blockNumber)
    {
        // remaining cache >= blockNumber
        bcos::WriteGuard l(x_blockNumber2Hash);
        std::erase_if(m_blockNumber2Hash,
            [blockNumber](const auto& item) { return item.first < blockNumber; });
    }

    bcos::crypto::HashType fetchBlockHash(bcos::protocol::BlockNumber _blockNumber)
    {
        EXECUTOR_LOG(TRACE) << LOG_BADGE("LedgerCache") << "fetchBlockHash start"
                            << LOG_KV("blockNumber", _blockNumber);
        {
            bcos::ReadGuard l(x_blockNumber2Hash);
            if (m_blockNumber2Hash.contains(_blockNumber))
            {
                auto& hash = m_blockNumber2Hash.at(_blockNumber);
                EXECUTOR_LOG(TRACE)
                    << LOG_BADGE("LedgerCache") << "fetchBlockHash return cache"
                    << LOG_KV("blockNumber", _blockNumber) << LOG_KV("hash", hash.abridged());
                return hash;
            }
        }

        auto hash = task::syncWait(ledger::getBlockHash(*m_ledger, _blockNumber));
        EXECUTOR_LOG(TRACE) << LOG_BADGE("LedgerCache")
                            << "fetchBlockHash return by fetching from ledger"
                            << LOG_KV("blockNumber", _blockNumber)
                            << LOG_KV("hash", hash.abridged());
        return hash;
    }

    uint64_t fetchTxGasLimit()
    {
        EXECUTOR_LOG(TRACE) << LOG_BADGE("LedgerCache") << "fetchTxGasLimit start";
        std::promise<uint64_t> txGasLimitFuture;

        m_ledger->asyncGetSystemConfigByKey(ledger::SYSTEM_KEY_TX_GAS_LIMIT,
            [&txGasLimitFuture](
                Error::Ptr error, std::string config, protocol::BlockNumber _number) mutable {
                if (error)
                {
                    EXECUTOR_LOG(ERROR)
                        << LOG_BADGE("LedgerCache")
                        << "fetchTxGasLimit error: " << LOG_KV("code", error->errorCode())
                        << LOG_KV("message", error->errorMessage());
                    txGasLimitFuture.set_value(0);
                }
                else
                {
                    txGasLimitFuture.set_value(boost::lexical_cast<uint64_t>(config));
                    EXECUTOR_LOG(TRACE)
                        << LOG_BADGE("LedgerCache") << "fetchTxGasLimit finish"
                        << LOG_KV("txGasLimit", config) << LOG_KV("blockNumber", _number);
                }
            });
        auto txGasLimit = txGasLimitFuture.get_future().get();
        return txGasLimit;
    }

    evmc_uint256be chainId()
    {
        // Note: the ledger-config is updated after every block committed
        if (m_ledgerConfig != nullptr && m_ledgerConfig->chainId().has_value())
        {
            return m_ledgerConfig->chainId().value();
        }
        // fetch the chainID if missed
        if (auto value = task::syncWait(
                ledger::getSystemConfig(*m_ledger, ledger::SYSTEM_KEY_WEB3_CHAIN_ID)))
        {
            auto numChainID = boost::lexical_cast<u256>(std::get<0>(*value));
            EXECUTOR_LOG(INFO) << LOG_DESC("fetchChainId success") << LOG_KV("chainId", numChainID);
            return bcos::toEvmC(numChainID);
        }

        return {};
    }
};
}  // namespace bcos::executor
