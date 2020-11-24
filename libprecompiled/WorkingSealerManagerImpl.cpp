/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @brief : Implementation of working sealer management
 * @file: WorkingSealerManagerImpl.cpp
 * @author: yujiechen
 * @date: 2020-06-10
 */

#include "WorkingSealerManagerImpl.h"
#include "Common.h"
#include "ffi_vrf.h"
#include <libblockverifier/ExecutiveContext.h>

using namespace bcos::precompiled;
using namespace bcos::blockverifier;
using namespace bcos::storage;

bool VRFInfo::isValidVRFPublicKey()
{
    return (curve25519_vrf_is_valid_pubkey(m_vrfPublicKey.c_str()) == 0);
}

bool VRFInfo::verifyProof()
{
    return (
        curve25519_vrf_verify(m_vrfPublicKey.c_str(), m_vrfInput.c_str(), m_vrfProof.c_str()) == 0);
}

bcos::h256 VRFInfo::getHashFromProof()
{
    std::string vrfHash = curve25519_vrf_proof_to_hash(m_vrfProof.c_str());
    return bcos::h256(fromHex(vrfHash));
}

WorkingSealerManagerImpl::WorkingSealerManagerImpl(
    std::shared_ptr<bcos::blockverifier::ExecutiveContext> _context, Address const& _origin,
    Address const& _sender)
  : m_context(_context),
    m_origin(_origin),
    m_sender(_sender),
    m_pendingSealerList(std::make_shared<bcos::h512s>()),
    m_workingSealerList(std::make_shared<bcos::h512s>()),
    m_skipAccessCheckOption(std::make_shared<AccessOptions>(m_origin, false))
{}

void WorkingSealerManagerImpl::createVRFInfo(
    std::string const& _vrfProof, std::string const& _vrfPublicKey, std::string const& _vrfInput)
{
    m_vrfInfo = std::make_shared<VRFInfo>(_vrfProof, _vrfPublicKey, _vrfInput);
}

void WorkingSealerManagerImpl::rotateWorkingSealer()
{
    if (!shouldRotate())
    {
        return;
    }
    try
    {
        // check VRFInfos firstly
        checkVRFInfos();
    }
    catch (PrecompiledException& e)
    {
        PRECOMPILED_LOG(WARNING) << LOG_DESC("rotateWorkingSealer failed for checkVRFInfos failed")
                                 << LOG_DESC("notifyNextLeaderRotate now");
        setSystemConfigByKey(
            INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE, "1", m_context->blockInfo().number + 1);
        throw;
    }
    // a valid transaction, reset the INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE flag
    tryToResetNotifyNextLeaderFlag();
    int64_t sealersNum = m_workingSealerList->size() + m_pendingSealerList->size();
    if (sealersNum <= 1 ||
        (m_configuredEpochSealersSize == sealersNum && 0 == m_pendingSealerList->size()))
    {
        PRECOMPILED_LOG(DEBUG)
            << LOG_DESC("No need to rotateWorkingSealer for all the sealers are working sealers")
            << LOG_KV("workingSealerNum", m_workingSealerList->size())
            << LOG_KV("pendingSealerNum", m_pendingSealerList->size());
        return;
    }
    // calculate the number of working sealers need to be removed and inserted
    auto nodeRotatingInfo = calNodeRotatingInfo();
    // get hash through VRF proof
    m_proofHash = m_vrfInfo->getHashFromProof();

    if (nodeRotatingInfo->removedWorkingSealerNum > 0)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC(
                                      "rotateWorkingSealer: rotate workingSealers into sealers")
                               << LOG_KV("rotatedCount", nodeRotatingInfo->removedWorkingSealerNum);
        // select working sealers to be removed
        // Note: Since m_workingSealerList will not be used afterwards,
        //       after updating the node type, it is not updated
        auto workingSealersToRemove =
            selectNodesFromList(m_workingSealerList, nodeRotatingInfo->removedWorkingSealerNum);
        // update node type from workingSealer to sealer
        for (auto const& node : *workingSealersToRemove)
        {
            UpdateNodeType(node, NODE_TYPE_SEALER);
            PRECOMPILED_LOG(DEBUG) << LOG_DESC("rotateWorkingSealer: remove workingSealer")
                                   << LOG_KV("nodeId", node.abridged());
        }
    }

    if (nodeRotatingInfo->insertedWorkingSealerNum > 0)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC(
                                      "rotateWorkingSealer: rotate sealers into workingSealers")
                               << LOG_KV(
                                      "rotatedCount", nodeRotatingInfo->insertedWorkingSealerNum);
        // select working sealers to be inserted
        auto workingSealersToInsert =
            selectNodesFromList(m_pendingSealerList, nodeRotatingInfo->insertedWorkingSealerNum);
        // Note: Since m_pendingSealerList will not be used afterwards,
        //       after updating the node type, it is not updated
        for (auto const& node : *workingSealersToInsert)
        {
            PRECOMPILED_LOG(DEBUG) << LOG_DESC("rotateWorkingSealer: insert workingSealer")
                                   << LOG_KV("nodeId", node.abridged());
            UpdateNodeType(node, NODE_TYPE_WORKING_SEALER);
        }
    }
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("rotateWorkingSealer succ")
                           << LOG_KV("insertedWorkingSealers",
                                  nodeRotatingInfo->insertedWorkingSealerNum)
                           << LOG_KV("removedWorkingSealers",
                                  nodeRotatingInfo->removedWorkingSealerNum);
}

void WorkingSealerManagerImpl::UpdateNodeType(bcos::h512 const& _node, std::string const& _nodeType)
{
    auto condition = m_consTable->newCondition();
    auto hexNode = toHexString(_node);
    condition->EQ(NODE_KEY_NODEID, *hexNode);
    // select the entries
    auto entries = m_consTable->select(PRI_KEY, condition);
    // select failed
    if (!entries || entries->size() == 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("UpdateNodeType: no corresponding node record was found")
                               << LOG_KV("nodeId", _node.abridged())
                               << LOG_KV("targetType", _nodeType);
        BOOST_THROW_EXCEPTION(
            PrecompiledException("update " + *hexNode + " type to " + _nodeType +
                                 " failed for no corresponding node record was found"));
    }
    // select succ
    auto entry = m_consTable->newEntry();
    entry->setField(NODE_TYPE, _nodeType);
    entry->setField(PRI_COLUMN, PRI_KEY);
    entry->setField(
        NODE_KEY_ENABLENUM, boost::lexical_cast<std::string>(m_context->blockInfo().number + 1));

    // not check the access option
    auto count = m_consTable->update(PRI_KEY, entry, condition, m_skipAccessCheckOption);
    if (count < 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("UpdateNodeType: update table failed")
                               << LOG_KV("nodeId", _node.abridged())
                               << LOG_KV("targetType", _nodeType);
        BOOST_THROW_EXCEPTION(PrecompiledException(
            "update " + *hexNode + " type to " + _nodeType + " failed for update table failed"));
    }
}


std::shared_ptr<bcos::h512s> WorkingSealerManagerImpl::selectNodesFromList(
    std::shared_ptr<bcos::h512s> _nodeList, int64_t _selectedNode)
{
    // sort the nodeList firstly
    std::sort(_nodeList->begin(), _nodeList->end());

    auto proofHashValue = u256(m_proofHash);
    int64_t nodeListSize = _nodeList->size();
    // shuffle _nodeList
    for (auto i = nodeListSize - 1; i > 0; i--)
    {
        auto selectedIdx = (int64_t)(proofHashValue % (i + 1));
        std::swap((*_nodeList)[i], (*_nodeList)[selectedIdx]);
        // update proofHashValue
        proofHashValue = u256(crypto::Hash(std::to_string(selectedIdx)));
    }
    // get the selected node list from the shuffled _nodeList
    std::shared_ptr<bcos::h512s> selectedNodeList = std::make_shared<bcos::h512s>();
    for (int64_t i = 0; i < _selectedNode; i++)
    {
        selectedNodeList->push_back((*_nodeList)[i]);
    }
    return selectedNodeList;
}

void WorkingSealerManagerImpl::getSealerList()
{
    if (m_sealerListObtained)
    {
        return;
    }

    m_consTable = openTable(m_context, SYS_CONSENSUS);
    if (!m_consTable)
    {
        BOOST_THROW_EXCEPTION(
            PrecompiledException("Open table failed, tableName:" + SYS_CONSENSUS));
    }
    auto blockNumber = m_context->blockInfo().number;
    *m_pendingSealerList = getNodeListByType(m_consTable, blockNumber, NODE_TYPE_SEALER);
    *m_workingSealerList = getNodeListByType(m_consTable, blockNumber, NODE_TYPE_WORKING_SEALER);
    m_sealerListObtained = true;
}

void WorkingSealerManagerImpl::checkPermission(
    Address const& _origin, bcos::h512s const& _allowedAccountInfoList)
{
    for (auto const& _allowedAccount : _allowedAccountInfoList)
    {
        if (toAddress(_allowedAccount) == _origin)
        {
            return;
        }
    }
    BOOST_THROW_EXCEPTION(
        PrecompiledException("Permission denied, must be among the working sealer list! origin:" +
                             _origin.hexPrefixed()));
}

void WorkingSealerManagerImpl::checkVRFInfos()
{
    if (!m_vrfInfo)
    {
        BOOST_THROW_EXCEPTION(
            PrecompiledException("No VRFProof provided! sender:" + m_origin.hexPrefixed()));
    }
    // check vrf input: must be equal to the parent block hash
    auto parentBlockHash = m_context->blockInfo().hash;
    if (bcos::h256(fromHex(m_vrfInfo->vrfInput())) != parentBlockHash)
    {
        PRECOMPILED_LOG(ERROR)
            << LOG_DESC("checkVRFInfos: Invalid VRFInput, must be the parent block hash")
            << LOG_KV("parentHash", parentBlockHash.abridged())
            << LOG_KV("vrfInput", m_vrfInfo->vrfInput());
        BOOST_THROW_EXCEPTION(PrecompiledException(
            "Invalid VRFInput, must be the parentHash! sender:" + m_origin.hexPrefixed()));
    }

    // check vrf public key: must be valid
    if (!m_vrfInfo->isValidVRFPublicKey())
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("checkVRFInfos: Invalid VRF public key")
                               << LOG_KV("publicKey", m_vrfInfo->vrfPublicKey());
        BOOST_THROW_EXCEPTION(
            PrecompiledException("Invalid VRF Public Key ! sender:" + m_origin.hexPrefixed() +
                                 ", publicKey:" + m_vrfInfo->vrfPublicKey()));
    }
    // verify vrf proof
    if (!m_vrfInfo->verifyProof())
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("checkVRFInfos: VRF proof verify failed");
        BOOST_THROW_EXCEPTION(
            PrecompiledException("Verify VRF proof failed! sender:" + m_origin.hexPrefixed()));
    }
    getSealerList();
    // check origin: the origin must be among the workingSealerList
    if (m_workingSealerList->size() > 0)
    {
        checkPermission(m_origin, *m_workingSealerList);
    }
}

// notify the next leader rotate when checkVRFInfos failed
void WorkingSealerManagerImpl::setSystemConfigByKey(
    std::string const& _key, std::string const& _value, int64_t _enableNumber)
{
    if (!m_sysConfigTable)
    {
        m_sysConfigTable = openTable(m_context, SYS_CONFIG);
        if (!m_sysConfigTable)
        {
            BOOST_THROW_EXCEPTION(PrecompiledException(
                "Open system configuration table failed! tableName: " + SYS_CONFIG));
        }
    }
    auto condition = m_sysConfigTable->newCondition();
    auto entries = m_sysConfigTable->select(_key, condition);
    auto entry = m_sysConfigTable->newEntry();
    entry->setField(SYSTEM_CONFIG_KEY, _key);
    entry->setField(SYSTEM_CONFIG_VALUE, _value);
    entry->setField(SYSTEM_CONFIG_ENABLENUM, boost::lexical_cast<std::string>(_enableNumber));

    if (entries->size() == 0u)
    {
        m_sysConfigTable->insert(_key, entry, m_skipAccessCheckOption);
    }
    else
    {
        m_sysConfigTable->update(_key, entry, condition, m_skipAccessCheckOption);
    }
}

// reset INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE to false when the next leader rotate succeed
void WorkingSealerManagerImpl::tryToResetNotifyNextLeaderFlag()
{
    // INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE is false
    if (!m_notifyNextLeaderRotateSetted)
    {
        return;
    }
    PRECOMPILED_LOG(INFO)
        << LOG_DESC("tryToResetNotifyNextLeaderFlag")
        << LOG_DESC("reset notifyRotate flag to false after working sealers rotating succeed");
    // update INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE to false
    setSystemConfigByKey(INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE, "0", m_context->blockInfo().number + 1);
}

bool WorkingSealerManagerImpl::shouldRotate()
{
    // open system configuration table
    m_sysConfigTable = openTable(m_context, SYS_CONFIG);
    if (!m_sysConfigTable)
    {
        BOOST_THROW_EXCEPTION(PrecompiledException(
            "Open system configuration table failed! tableName: " + SYS_CONFIG));
    }
    auto notifyRotateFlagInfo = getSysteConfigByKey(
        m_sysConfigTable, INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE, m_context->blockInfo().number);
    m_notifyNextLeaderRotateSetted = false;
    if (notifyRotateFlagInfo->first != "")
    {
        m_notifyNextLeaderRotateSetted =
            (bool)(boost::lexical_cast<int64_t>(notifyRotateFlagInfo->first));
    }
    if (m_notifyNextLeaderRotateSetted)
    {
        return true;
    }
    // get epoch_sealer_num from the table
    auto epochSealersInfo = getSysteConfigByKey(
        m_sysConfigTable, SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, m_context->blockInfo().number);
    m_configuredEpochSealersSize = boost::lexical_cast<int64_t>(epochSealersInfo->first);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("shouldRotate: get epoch_sealer_num")
                           << LOG_KV("key", SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM)
                           << LOG_KV("value", epochSealersInfo->first)
                           << LOG_KV("enableBlk", epochSealersInfo->second)
                           << LOG_KV("epochSealersNum", m_configuredEpochSealersSize);
    // the epoch_sealer_num has updated in the previous block
    if (epochSealersInfo->second == m_context->blockInfo().number)
    {
        return true;
    }
    getSealerList();
    int64_t sealersNum = m_pendingSealerList->size() + m_workingSealerList->size();
    auto maxWorkingSealerNum = std::min(m_configuredEpochSealersSize, sealersNum);
    if ((int64_t)(m_workingSealerList->size()) < maxWorkingSealerNum)
    {
        return true;
    }
    // get epoch_block_num from the table
    auto epochBlockInfo = getSysteConfigByKey(
        m_sysConfigTable, SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, m_context->blockInfo().number);
    auto epochBlockNum = boost::lexical_cast<int64_t>(epochBlockInfo->first);
    if ((m_context->blockInfo().number - epochBlockInfo->second) % epochBlockNum == 0)
    {
        return true;
    }
    PRECOMPILED_LOG(WARNING)
        << LOG_DESC("should not rotate working sealers for not meet the requirements")
        << LOG_KV("epochBlockNum", epochBlockNum) << LOG_KV("curNum", m_context->blockInfo().number)
        << LOG_KV("enableNum", epochBlockInfo->second) << LOG_KV("sealersNum", sealersNum)
        << LOG_KV("epochSealersNum", m_configuredEpochSealersSize);
    return false;
}

NodeRotatingInfo::Ptr WorkingSealerManagerImpl::calNodeRotatingInfo()
{
    int64_t sealersNum = m_pendingSealerList->size() + m_workingSealerList->size();
    // get rPBFT epoch_sealer_num
    auto epochSize = std::min(m_configuredEpochSealersSize, sealersNum);

    int64_t workingSealersNum = m_workingSealerList->size();

    auto nodeRotatingInfo = std::make_shared<NodeRotatingInfo>();
    if (workingSealersNum > epochSize)
    {
        nodeRotatingInfo->removedWorkingSealerNum = (workingSealersNum - epochSize);
        nodeRotatingInfo->insertedWorkingSealerNum = 0;
    }
    // The configurated workingSealers size is larger than the current workingSealers size
    else if (workingSealersNum < epochSize)
    {
        nodeRotatingInfo->removedWorkingSealerNum = 0;
        nodeRotatingInfo->insertedWorkingSealerNum = epochSize - workingSealersNum;
    }
    // The configurated workingSealers size is equal to the current workingSealers size
    else
    {
        if (sealersNum == workingSealersNum)
        {
            nodeRotatingInfo->removedWorkingSealerNum = 0;
            nodeRotatingInfo->insertedWorkingSealerNum = 0;
        }
        else
        {
            nodeRotatingInfo->removedWorkingSealerNum = 1;
            nodeRotatingInfo->insertedWorkingSealerNum = 1;
        }
    }
    PRECOMPILED_LOG(INFO) << LOG_DESC("calNodeRotatingInfo") << LOG_KV("sealersNum", sealersNum)
                          << LOG_KV("configuredEpochSealers", m_configuredEpochSealersSize)
                          << LOG_KV("toRemovedNode", nodeRotatingInfo->removedWorkingSealerNum)
                          << LOG_KV("toInsertedNode", nodeRotatingInfo->insertedWorkingSealerNum);
    return nodeRotatingInfo;
}
