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
 * @file WorkingSealerManagerImpl.cpp
 * @author: kyonGuo
 * @date 2023/2/13
 */

#include "WorkingSealerManagerImpl.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace bcos::ledger;

void WorkingSealerManagerImpl::createVRFInfo(
    std::string _vrfProof, std::string _vrfPublicKey, std::string _vrfInput)
{
    m_vrfInfo = std::make_shared<VRFInfo>(
        std::move(_vrfProof), std::move(_vrfPublicKey), std::move(_vrfInput));
}

void WorkingSealerManagerImpl::rotateWorkingSealer(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    if (!shouldRotate(_executive))
    {
        return;
    }
    auto parentHash = _executive->blockContext().lock()->parentHash();
    try
    {
        getConsensusNodeListFromStorage(_executive);
        checkVRFInfos(parentHash, _callParameters->m_origin);
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(WARNING) << LOG_DESC("rotateWorkingSealer failed for checkVRFInfos failed")
                                 << LOG_DESC("notifyNextLeaderRotate now");
        setNotifyRotateFlag(_executive, 1);
        throw;
    }
    //  a valid transaction, reset the INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE flag
    if (m_notifyNextLeaderRotateSet)
    {
        setNotifyRotateFlag(_executive, 0);
    }
    uint32_t sealersNum = m_workingSealer.size() + m_pendingSealer.size();
    if (sealersNum <= 1 || (m_configuredEpochSealersSize == sealersNum && m_pendingSealer.empty()))
    {
        PRECOMPILED_LOG(DEBUG)
            << LOG_DESC("No need to rotateWorkingSealer for all the sealers are working sealers")
            << LOG_KV("workingSealerNum", m_workingSealer.size())
            << LOG_KV("pendingSealerNum", m_pendingSealer.size());
        return;
    }
    // calculate the number of working sealers need to be removed and inserted
    auto [insertedWorkingSealerNum, removedWorkingSealerNum] = calNodeRotatingInfo();
    if (removedWorkingSealerNum > 0)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC(
                                      "rotateWorkingSealer: rotate workingSealers into sealers")
                               << LOG_KV("rotatedCount", removedWorkingSealerNum);
        // select working sealers to be removed
        // Note: Since m_workingSealerList will not be used afterwards,
        //       after updating the node type, it is not updated
        auto workingSealersToRemove = selectNodesFromList(m_workingSealer, removedWorkingSealerNum);
        // update node type from workingSealer to sealer
        updateNodeListType(
            std::move(workingSealersToRemove), std::string(CONSENSUS_SEALER), _executive);
    }

    if (insertedWorkingSealerNum > 0)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC(
                                      "rotateWorkingSealer: rotate sealers into workingSealers")
                               << LOG_KV("rotatedCount", insertedWorkingSealerNum);
        // select working sealers to be inserted
        auto workingSealersToInsert =
            selectNodesFromList(m_pendingSealer, insertedWorkingSealerNum);
        // Note: Since m_pendingSealerList will not be used afterwards,
        //       after updating the node type, it is not updated
        updateNodeListType(
            std::move(workingSealersToInsert), std::string(CONSENSUS_WORKING_SEALER), _executive);
    }

    commitConsensusNodeListToStorage(_executive);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("rotateWorkingSealer succ")
                           << LOG_KV("insertedWorkingSealers", insertedWorkingSealerNum)
                           << LOG_KV("removedWorkingSealers", removedWorkingSealerNum);
}

void WorkingSealerManagerImpl::checkVRFInfos(HashType const& parentHash, std::string const& origin)
{
    if (HashType(m_vrfInfo->vrfInput()) != parentHash)
    {
        PRECOMPILED_LOG(ERROR)
            << LOG_DESC("checkVRFInfos: Invalid VRFInput, must be the parent block hash")
            << LOG_KV("parentHash", parentHash.abridged())
            << LOG_KV("vrfInput", m_vrfInfo->vrfInput()) << LOG_KV("origin", origin);
        BOOST_THROW_EXCEPTION(PrecompiledError("Invalid VRFInput, must be the parentHash!"));
    }
    // check vrf public key: must be valid
    if (!m_vrfInfo->isValidVRFPublicKey())
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("checkVRFInfos: Invalid VRF public key")
                               << LOG_KV("publicKey", m_vrfInfo->vrfPublicKey());
        BOOST_THROW_EXCEPTION(
            PrecompiledError("Invalid VRF Public Key!, publicKey:" + m_vrfInfo->vrfPublicKey()));
    }
    // verify vrf proof
    if (!m_vrfInfo->verifyProof())
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("checkVRFInfos: VRF proof verify failed")
                               << LOG_KV("origin", origin);
        BOOST_THROW_EXCEPTION(PrecompiledError("Verify VRF proof failed!"));
    }
    // check origin: the origin must be among the workingSealerList
    if (!m_workingSealer.empty())
    {
        if (!std::any_of(m_workingSealer.begin(), m_workingSealer.end(),
                [&origin](auto const& node) { return node == origin; })) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(
                PrecompiledError("Permission denied, must be among the working sealer list!"));
        }
    }
}

bool WorkingSealerManagerImpl::shouldRotate(const executor::TransactionExecutive::Ptr& _executive)
{
    auto blockContext = _executive->blockContext().lock();
    auto entry = _executive->storage().getRow(
        ledger::SYS_CONSENSUS, ledger::INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE);
    m_notifyNextLeaderRotateSet = false;
    m_notifyNextLeaderRotateSet = getNotifyRotateFlag(_executive);
    if (m_notifyNextLeaderRotateSet)
    {
        return true;
    }
    auto epochEntry =
        _executive->storage().getRow(ledger::SYS_CONFIG, ledger::SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM);
    if (epochEntry) [[likely]]
    {
        auto epochInfo = epochEntry->getObject<ledger::SystemConfigEntry>();
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("shouldRotate: get epoch_sealer_num")
                               << LOG_KV("value", std::get<0>(epochInfo))
                               << LOG_KV("enableBlk", std::get<1>(epochInfo));
        m_configuredEpochSealersSize = boost::lexical_cast<uint32_t>(std::get<0>(epochInfo));
        if (std::get<1>(epochInfo) == blockContext->number())
        {
            return true;
        }
    }
    uint32_t sealerNum = m_pendingSealer.size() + m_workingSealer.size();
    auto maxWorkingSealerNum = std::min(m_configuredEpochSealersSize, sealerNum);
    if (m_workingSealer.size() < maxWorkingSealerNum)
    {
        return true;
    }
    auto epochBlockInfo =
        _executive->storage().getRow(ledger::SYS_CONFIG, ledger::SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM);

    auto epochBlockNumEntry = epochBlockInfo->getObject<ledger::SystemConfigEntry>();
    auto epochBlockNum = boost::lexical_cast<uint32_t>(std::get<0>(epochBlockNumEntry));
    if (blockContext->number() - std::get<1>(epochBlockNumEntry) % epochBlockNum == 0)
    {
        return true;
    }
    PRECOMPILED_LOG(WARNING)
        << LOG_DESC("should not rotate working sealers for not meet the requirements")
        << LOG_KV("epochBlockNum", epochBlockNum) << LOG_KV("curNum", blockContext->number())
        << LOG_KV("enableNum", std::get<1>(epochBlockNumEntry)) << LOG_KV("sealerNum", sealerNum)
        << LOG_KV("epochSealersNum", m_configuredEpochSealersSize);

    return false;
}

void WorkingSealerManagerImpl::getConsensusNodeListFromStorage(
    const executor::TransactionExecutive::Ptr& _executive)
{
    auto blockContext = _executive->blockContext().lock();
    auto entry = _executive->storage().getRow(ledger::SYS_CONSENSUS, "key");
    auto consensusNodeList = entry->getObject<ledger::ConsensusNodeList>();
    for (const auto& node : consensusNodeList)
    {
        if (boost::lexical_cast<BlockNumber>(node.enableNumber) > blockContext->number())
            [[unlikely]]
        {
            continue;
        }
        if (node.type == ledger::CONSENSUS_SEALER)
        {
            m_pendingSealer.push_back(node.nodeID);
        }
        if (node.type == ledger::CONSENSUS_WORKING_SEALER)
        {
            m_workingSealer.push_back(node.nodeID);
        }
    }
    m_consensusNodes.swap(consensusNodeList);
}

void WorkingSealerManagerImpl::setNotifyRotateFlag(
    const executor::TransactionExecutive::Ptr& executive, unsigned flag)
{
    auto blockContext = executive->blockContext().lock();
    storage::Entry rotateEntry;
    rotateEntry.setObject(
        SystemConfigEntry{boost::lexical_cast<std::string>(flag), blockContext->number() + 1});
    executive->storage().setRow(
        SYS_CONSENSUS, INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE, std::move(rotateEntry));
}

bool WorkingSealerManagerImpl::getNotifyRotateFlag(
    const executor::TransactionExecutive::Ptr& executive)
{
    auto blockContext = executive->blockContext().lock();
    auto entry = executive->storage().getRow(SYS_CONSENSUS, INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE);
    if (entry) [[likely]]
    {
        auto config = entry->getObject<ledger::SystemConfigEntry>();
        if (!std::get<0>(config).empty() && blockContext->number() >= std::get<1>(config))
        {
            return boost::lexical_cast<bool>(std::get<0>(config));
        }
    }
    return false;
}

std::tuple<uint32_t, uint32_t> WorkingSealerManagerImpl::calNodeRotatingInfo()
{
    uint32_t sealersNum = m_pendingSealer.size() + m_workingSealer.size();
    // get rPBFT epoch_sealer_num
    auto epochSize = std::min(m_configuredEpochSealersSize, sealersNum);

    uint32_t workingSealersNum = m_workingSealer.size();
    uint32_t insertedWorkingSealerNum = 0;
    uint32_t removedWorkingSealerNum = 0;
    if (workingSealersNum > epochSize)
    {
        insertedWorkingSealerNum = 0;
        removedWorkingSealerNum = workingSealersNum - epochSize;
    }
    else if (workingSealersNum < epochSize)
    {
        insertedWorkingSealerNum = epochSize - workingSealersNum;
        removedWorkingSealerNum = 0;
    }
    else
    {
        // not all sealer are working, then rotate 1 sealer
        if (sealersNum != workingSealersNum)
        {
            insertedWorkingSealerNum = 1;
            removedWorkingSealerNum = 1;
        }
    }
    PRECOMPILED_LOG(INFO) << LOG_DESC("calNodeRotatingInfo") << LOG_KV("sealersNum", sealersNum)
                          << LOG_KV("configuredEpochSealers", m_configuredEpochSealersSize);
    return {insertedWorkingSealerNum, removedWorkingSealerNum};
}

std::unique_ptr<std::vector<std::string>> WorkingSealerManagerImpl::selectNodesFromList(
    std::vector<std::string>& _nodeList, uint32_t _selectNum)
{
    std::sort(_nodeList.begin(), _nodeList.end());

    auto proofHashValue = u256(m_vrfInfo->getHashFromProof());

    // shuffle _nodeList
    for (auto i = _nodeList.size() - 1; i > 0; i--)
    {
        auto selectedIdx = (int64_t)(proofHashValue % (i + 1));
        std::swap((_nodeList)[i], (_nodeList)[selectedIdx]);
        // update proofHashValue
        proofHashValue = u256(GlobalHashImpl::g_hashImpl->hash(std::to_string(selectedIdx)));
    }
    // get the selected node list from the shuffled _nodeList
    auto selectedNodeList = std::make_unique<std::vector<std::string>>();
    selectedNodeList->reserve(_selectNum);
    for (uint32_t i = 0; i < _selectNum; ++i)
    {
        selectedNodeList->push_back(_nodeList[i]);
    }
    return selectedNodeList;
}

void WorkingSealerManagerImpl::updateNodeListType(
    std::unique_ptr<std::vector<std::string>> _nodeList, std::string const& _type,
    const executor::TransactionExecutive::Ptr& executive)
{
    auto blockContext = executive->blockContext().lock();

    m_consensusChangeFlag = !_nodeList->empty();
    for (const auto& node : (*_nodeList))
    {
        auto it = std::find_if(m_consensusNodes.begin(), m_consensusNodes.end(),
            [&node](const ConsensusNode& consensusNode) { return consensusNode.nodeID == node; });
        if (it != m_consensusNodes.end()) [[likely]]
        {
            it->type = _type;
            it->enableNumber = boost::lexical_cast<std::string>(blockContext->number() + 1);
        }
    }
}
