/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file WorkingSealerManagerImpl.h
 * @author: kyonGuo
 * @date 2023/2/13
 */

#pragma once
#include "../../executive/BlockContext.h"
#include "../../vm/Precompiled.h"
#include "VRFInfo.h"
#include <bcos-framework/storage/Table.h>
#include <bcos-tool/ConsensusNode.h>

namespace bcos::precompiled
{

struct WorkingSealer
{
    std::string nodeID;
    uint64_t termWeight;
};

class WorkingSealerManagerImpl
{
public:
    WorkingSealerManagerImpl(const WorkingSealerManagerImpl&) = default;
    WorkingSealerManagerImpl(WorkingSealerManagerImpl&&) = default;
    WorkingSealerManagerImpl& operator=(const WorkingSealerManagerImpl&) = default;
    WorkingSealerManagerImpl& operator=(WorkingSealerManagerImpl&&) = default;
    using Ptr = std::shared_ptr<WorkingSealerManagerImpl>;
    WorkingSealerManagerImpl(bool withWeight);
    ~WorkingSealerManagerImpl() = default;

    void createVRFInfo(bytes _vrfProof, bytes _vrfPublicKey, bytes _vrfInput);
    void rotateWorkingSealer(const executor::TransactionExecutive::Ptr& _executive,
        const PrecompiledExecResult::Ptr& _callParameters);

private:
    void checkVRFInfos(crypto::HashType const& parentHash, std::string const& origin);
    bool shouldRotate(const executor::TransactionExecutive::Ptr& _executive);
    bool getConsensusNodeListFromStorage(const executor::TransactionExecutive::Ptr& _executive);

    static void setNotifyRotateFlag(
        const executor::TransactionExecutive::Ptr& executive, unsigned flag);
    static bool getNotifyRotateFlag(const executor::TransactionExecutive::Ptr& executive);

    // calculate the number of working sealers that need to be added and removed
    std::tuple<uint32_t, uint32_t> calNodeRotatingInfo();
    std::vector<WorkingSealer> selectNodesFromList(
        std::vector<WorkingSealer>& _nodeList, uint32_t _selectNum);

    // update node list type in m_consensusNodes
    void updateNodeListType(const std::vector<WorkingSealer>& _nodeList, std::string const& _type,
        const executor::TransactionExecutive::Ptr& executive);
    void commitConsensusNodeListToStorage(const executor::TransactionExecutive::Ptr& _executive);

    VRFInfo::Ptr m_vrfInfo;
    std::vector<WorkingSealer> m_candidateSealer;
    std::vector<WorkingSealer> m_consensusSealer;
    bool m_consensusChangeFlag = false;
    ledger::ConsensusNodeList m_consensusNodes;
    bool m_notifyNextLeaderRotateSet = false;
    uint32_t m_configuredEpochSealersSize = 4;
    bool m_withWeight{};
};
}  // namespace bcos::precompiled
