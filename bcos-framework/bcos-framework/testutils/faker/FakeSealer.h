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
 * @brief fake sealer
 * @file FakeSealer.h
 * @author: yujiechen
 * @date 2021-05-26
 */
#pragma once
#include "../../sealer/SealerInterface.h"
#include <atomic>
#include <memory>

using namespace bcos;
using namespace bcos::sealer;

namespace bcos
{
namespace test
{
class FakeSealer : public SealerInterface
{
public:
    using Ptr = std::shared_ptr<FakeSealer>;
    FakeSealer() = default;
    ~FakeSealer() override {}

    void start() override {}
    void stop() override {}

    void asyncNotifySealProposal(uint64_t _startIndex, uint64_t _endIndex, uint64_t _maxTxsToSeal,
        std::function<void(Error::Ptr)>) override
    {
        m_proposalStartIndex = _startIndex;
        m_proposalEndIndex = _endIndex;
        m_maxTxsToSeal = _maxTxsToSeal;
    }

    uint64_t unSealedTxsSize() const { return m_unSealedTxsSize; }

    uint64_t proposalStartIndex() const { return m_proposalStartIndex; }
    uint64_t proposalEndIndex() const { return m_proposalEndIndex; }
    uint64_t maxTxsToSeal() const { return m_maxTxsToSeal; }

    void asyncNoteLatestBlockNumber(int64_t _blockNumber) override { m_blockNumber = _blockNumber; }
    void asyncNoteLatestBlockTimestamp(int64_t _timestamp) override { m_timestamp = _timestamp; }
    int64_t blockNumber() const { return m_blockNumber; }
    void asyncResetSealing(std::function<void(Error::Ptr)>) override {}

private:
    std::atomic<uint64_t> m_unSealedTxsSize = {0};
    uint64_t m_proposalStartIndex = 0;
    uint64_t m_proposalEndIndex = 0;
    uint64_t m_maxTxsToSeal = 0;
    int64_t m_blockNumber;
    int64_t m_timestamp;
};
}  // namespace test
}  // namespace bcos