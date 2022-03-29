/**
 *  Copyright (C) 2021 bcos-sync.
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
 * @brief consensus faker
 * @file FakeConsensus.h
 * @author: yujiechen
 * @date 2021-06-08
 */
#pragma once
#include <bcos-framework/interfaces/consensus/ConsensusInterface.h>
#include <bcos-framework/interfaces/ledger/LedgerConfig.h>
using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace bcos::ledger;

namespace bcos
{
namespace test
{
class FakeConsensus : public ConsensusInterface
{
public:
    using Ptr = std::shared_ptr<FakeConsensus>;
    FakeConsensus() = default;
    ~FakeConsensus() override {}


    void start() override {}
    void stop() override {}

    // useless for bcos-sync
    void asyncSubmitProposal(
        bool, bytesConstRef, BlockNumber, HashType const&, std::function<void(Error::Ptr)>) override
    {}

    // useless for bcos-sync
    void asyncGetPBFTView(std::function<void(Error::Ptr, ViewType)>) override {}

    // the sync module calls this interface to check block
    void asyncCheckBlock(Block::Ptr, std::function<void(Error::Ptr, bool)> _onVerifyFinish) override
    {
        _onVerifyFinish(nullptr, m_checkBlockResult);
    }

    // the sync module calls this interface to notify new block
    void asyncNotifyNewBlock(
        LedgerConfig::Ptr _ledgerConfig, std::function<void(Error::Ptr)> _onRecv) override
    {
        m_ledgerConfig = _ledgerConfig;
        _onRecv(nullptr);
    }

    // useless for the sync module
    void asyncNotifyConsensusMessage(bcos::Error::Ptr, std::string const&, NodeIDPtr, bytesConstRef,
        std::function<void(Error::Ptr _error)>) override
    {}

    bool checkBlockResult() const { return m_checkBlockResult; }
    void setCheckBlockResult(bool _checkBlockResult) { m_checkBlockResult = _checkBlockResult; }

    LedgerConfig::Ptr ledgerConfig() { return m_ledgerConfig; }

    void notifyHighestSyncingNumber(bcos::protocol::BlockNumber) override {}

    void asyncNoteUnSealedTxsSize(uint64_t, std::function<void(Error::Ptr)>) override {}

    void asyncGetConsensusStatus(std::function<void(Error::Ptr, std::string)>) override {}
    void notifyConnectedNodes(
        bcos::crypto::NodeIDSet const&, std::function<void(Error::Ptr)>) override
    {}

private:
    std::atomic_bool m_checkBlockResult = {true};
    LedgerConfig::Ptr m_ledgerConfig;
};
}  // namespace test
}  // namespace bcos
