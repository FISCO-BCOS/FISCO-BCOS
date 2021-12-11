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
 * @brief StorageInterface for PBFT
 * @file PBFTStorage.cpp
 * @author: yujiechen
 * @date 2021-04-26
 */
#pragma once
#include "PBFTMessageInterface.h"
#include "PBFTProposalInterface.h"
#include <bcos-framework/interfaces/ledger/LedgerConfig.h>
#include <bcos-framework/interfaces/protocol/Block.h>
#include <bcos-framework/interfaces/protocol/BlockHeader.h>
#include <bcos-framework/interfaces/protocol/ProtocolTypeDef.h>
namespace bcos
{
namespace consensus
{
class PBFTStorage
{
public:
    using Ptr = std::shared_ptr<PBFTStorage>;
    PBFTStorage() = default;
    virtual ~PBFTStorage() {}

    virtual PBFTProposalListPtr loadState(bcos::protocol::BlockNumber _stabledIndex) = 0;
    virtual int64_t maxCommittedProposalIndex() = 0;
    virtual void asyncCommitProposal(PBFTProposalInterface::Ptr _commitProposal) = 0;
    virtual void asyncCommitStableCheckPoint(PBFTProposalInterface::Ptr _stableProposal) = 0;
    virtual void asyncRemoveStabledCheckPoint(size_t _stabledCheckPointIndex) = 0;

    // get the latest committed proposal from the storage
    virtual void asyncGetCommittedProposals(bcos::protocol::BlockNumber _start, size_t _offset,
        std::function<void(PBFTProposalListPtr)> _onSuccess) = 0;
    virtual void registerFinalizeHandler(
        std::function<void(bcos::ledger::LedgerConfig::Ptr, bool)> _finalizeHandler) = 0;
};
}  // namespace consensus
}  // namespace bcos