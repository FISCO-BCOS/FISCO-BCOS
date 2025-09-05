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
 * @file Sealer.h
 * @author: yujiechen
 * @date: 2021-05-14
 */
#pragma once
#include "SealerConfig.h"
#include "SealingManager.h"
#include "bcos-framework/sealer/SealerInterface.h"
#include <bcos-utilities/Worker.h>
#include <chrono>

namespace bcos::sealer
{
class Sealer : public Worker, public SealerInterface, public std::enable_shared_from_this<Sealer>
{
public:
    enum SealBlockResult : int8_t
    {
        FAILED = 0,
        SUCCESS = 1,
        WAIT_FOR_LATEST_BLOCK = 2,
    };
    using Ptr = std::shared_ptr<Sealer>;
    explicit Sealer(SealerConfig::Ptr _sealerConfig);
    ~Sealer() override = default;

    void start() override;
    void stop() override;

    // for consensus, to seal a proposal block tx hash list
    void asyncNotifySealProposal(uint64_t _proposalStartIndex, uint64_t _proposalEndIndex,
        uint64_t _maxTxsPerBlock, std::function<void(Error::Ptr)> _onRecvResponse) override;
    // hook for txpool, invoke when txpool storage size changed

    // for sys block
    void asyncNoteLatestBlockNumber(int64_t _blockNumber) override;
    void asyncNoteLatestBlockHash(crypto::HashType _hash) override;
    void asyncNoteLatestBlockTimestamp(int64_t _timestamp) override;
    // interface for the consensus module to notify reset the sealing transactions
    void asyncResetSealing(std::function<void(Error::Ptr)> _onRecvResponse) override;

    virtual void init(bcos::consensus::ConsensusInterface::Ptr _consensus);

    uint16_t hookWhenSealBlock([[maybe_unused]] bcos::protocol::Block::Ptr _block) override;
    void setFetchTimeout(int fetchTimeout);

    // only for test
    SealerConfig::Ptr sealerConfig() const;
    SealingManager::Ptr sealingManager() const;

    void executeWorker() override;
    void setSealingManager(SealingManager::Ptr _sealingManager);

protected:
    virtual void noteGenerateProposal();
    virtual void submitProposal(bool _containSysTxs, bcos::protocol::Block::Ptr _block);

    SealerConfig::Ptr m_sealerConfig;
    SealingManager::Ptr m_sealingManager;
    std::atomic_bool m_running = {false};

    boost::condition_variable m_signalled;
    // mutex to access m_signalled
    std::atomic<std::chrono::steady_clock::time_point> m_lastFetchTimepoint;
    int m_fetchTimeout = 5;  // Default timeout 5s
    boost::mutex x_signalled;
    bcos::crypto::Hash::Ptr m_hashImpl;

    std::chrono::steady_clock::time_point increaseLastFetchTimepoint();
};
}  // namespace bcos::sealer