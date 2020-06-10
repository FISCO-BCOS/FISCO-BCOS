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

using namespace dev::precompiled;
using namespace dev::blockverifier;
using namespace dev::storage;

bool VRFInfo::isValidVRFPublicKey()
{
    return (curve25519_vrf_is_valid_pubkey(m_vrfPublicKey.c_str()) == 0);
}

bool VRFInfo::verifyProof()
{
    return (
        curve25519_vrf_verify(m_vrfPublicKey.c_str(), m_vrfInput.c_str(), m_vrfProof.c_str()) == 0);
}

dev::h256 VRFInfo::getHashFromProof()
{
    std::string vrfHash = curve25519_vrf_proof_to_hash(m_vrfProof.c_str());
    return dev::h256(fromHex(vrfHash));
}

WorkingSealerManagerImpl::WorkingSealerManagerImpl(
    std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, Address const& _origin,
    Address const& _sender)
  : m_context(_context), m_origin(_origin), m_sender(_sender)
{
    m_pendingSealerList = std::make_shared<dev::h512s>();
    m_workingSealerList = std::make_shared<dev::h512s>();
}

void WorkingSealerManagerImpl::setVRFInfos(VRFInfo::Ptr _vrfInfo)
{
    m_vrfInfo = _vrfInfo;
}

void WorkingSealerManagerImpl::getSealerList()
{
    m_consTable = openTable(m_context, SYS_CONSENSUS);
    if (!m_consTable)
    {
        BOOST_THROW_EXCEPTION(
            PrecompiledException("Open table failed, tableName:" + SYS_CONSENSUS));
    }
    auto blockNumber = m_context->blockInfo().number;
    *m_pendingSealerList = getNodeListByType(m_consTable, blockNumber, NODE_TYPE_SEALER);
    *m_workingSealerList = getNodeListByType(m_consTable, blockNumber, NODE_TYPE_WORKING_SEALER);
}

bool WorkingSealerManagerImpl::checkPermission(
    Address const& _origin, dev::h512s const& _allowedAccountInfoList)
{
    for (auto const& _allowedAccount : _allowedAccountInfoList)
    {
        if (toAddress(_allowedAccount) == _origin)
        {
            return true;
        }
    }
    return false;
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
    if (dev::h256(fromHex(m_vrfInfo->vrfInput())) != parentBlockHash)
    {
        BOOST_THROW_EXCEPTION(PrecompiledException(
            "Invalid VRFInput, must be the parentHash! sender:" + m_origin.hexPrefixed()));
    }

    // check vrf public key: must be valid
    if (!m_vrfInfo->isValidVRFPublicKey())
    {
        BOOST_THROW_EXCEPTION(PrecompiledException(
            "Invalid VRF Public Key, must be the parentHash! sender:" + m_origin.hexPrefixed() +
            ", publicKey:" + m_vrfInfo->vrfPublicKey()));
    }
    // verify vrf proof
    if (!m_vrfInfo->verifyProof())
    {
        BOOST_THROW_EXCEPTION(
            PrecompiledException("Verify VRF proof failed! sender:" + m_origin.hexPrefixed()));
    }
    // check origin: must be among the workingSealerList, others cannot call this interface
    getSealerList();
    if (!checkPermission(m_origin, *m_workingSealerList))
    {
        BOOST_THROW_EXCEPTION(PrecompiledException(
            "Permission denied, must be among the working sealer list! origin:" +
            m_origin.hexPrefixed()));
    }
}

int64_t WorkingSealerManagerImpl::getrPBFTEpochSealersNum()
{
    // open system configuration table
    auto sysConfigTable = openTable(m_context, SYS_CONFIG);
    if (!sysConfigTable)
    {
        BOOST_THROW_EXCEPTION(PrecompiledException(
            "Open system configuration table failed! tableName: " + SYS_CONFIG));
    }
    // get epoch_sealers_num from the table
    auto epochSealersInfo = getSysteConfigByKey(
        sysConfigTable, SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, m_context->blockInfo().number);
    auto epochSealersSize = boost::lexical_cast<int64_t>(epochSealersInfo->first);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("getrPBFTEpochSealersNum")
                           << LOG_KV("key", SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM)
                           << LOG_KV("value", epochSealersInfo->first)
                           << LOG_KV("enableBlk", epochSealersInfo->second)
                           << LOG_KV("epochSealersNum", epochSealersSize);
    return epochSealersSize;
}

NodeRotatingInfo::Ptr WorkingSealerManagerImpl::calNodeRotatingInfo()
{
    int64_t sealersNum = m_pendingSealerList->size() + m_workingSealerList->size();
    int64_t configuredEpochSealers = getrPBFTEpochSealersNum();
    // get rPBFT epoch_sealers_num
    auto epochSize = std::min(configuredEpochSealers, sealersNum);

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
        nodeRotatingInfo->removedWorkingSealerNum = 1;
        nodeRotatingInfo->insertedWorkingSealerNum = 1;
    }
    PRECOMPILED_LOG(INFO) << LOG_DESC("calNodeRotatingInfo") << LOG_KV("sealersNum", sealersNum)
                          << LOG_KV("configuredEpochSealers", configuredEpochSealers)
                          << LOG_KV("toRemovedNode", nodeRotatingInfo->removedWorkingSealerNum)
                          << LOG_KV("toInsertedNode", nodeRotatingInfo->insertedWorkingSealerNum);
    return nodeRotatingInfo;
}