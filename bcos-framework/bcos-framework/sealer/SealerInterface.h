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
 * @brief interface for Sealer module
 * @file SealerInterface.h
 * @author: yujiechen
 * @date 2021-05-14
 */
#pragma once
#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include <bcos-utilities/Error.h>
#include <functional>


namespace bcos::sealer
{
class SealerInterface
{
public:
    using Ptr = std::shared_ptr<SealerInterface>;
    SealerInterface() = default;
    virtual ~SealerInterface() {}

    virtual void start() = 0;
    virtual void stop() = 0;

    // interface for the transaction module to notify the sealer seal new proposal
    virtual void asyncNotifySealProposal(uint64_t _proposalIndex, uint64_t _proposalEndIndex,
        uint64_t _maxTxsToSeal, std::function<void(Error::Ptr)> onRecvResponse) = 0;

    // interface for the consensus module to notify the latest block number
    virtual void asyncNoteLatestBlockNumber(int64_t _blockNumber) = 0;

    virtual void asyncNoteLatestBlockHash(crypto::HashType _hash) = 0;

    virtual void asyncNoteLatestBlockTimestamp(int64_t _timestamp) = 0;
    // interface for the consensus module to notify reset the sealing transactions
    virtual void asyncResetSealing(std::function<void(Error::Ptr)> _onRecvResponse) = 0;

    virtual uint16_t hookWhenSealBlock(bcos::protocol::Block::Ptr) = 0;

    virtual void asyncNotifyBlockResult(bcos::protocol::BlockNumber _blockNumber,
        bcos::protocol::TransactionSubmitResultsPtr _txsResult)
    {
        // default implementation
        throw std::runtime_error("Unimplemented!");
    }
};
}  // namespace bcos::sealer
