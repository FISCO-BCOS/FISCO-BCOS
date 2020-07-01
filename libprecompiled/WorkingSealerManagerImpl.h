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
 * @file: WorkingSealerManagerImpl.h
 * @author: yujiechen
 * @date: 2020-06-10
 */

#pragma once
#include <libdevcore/Address.h>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>

namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
}
namespace storage
{
class Table;
}

namespace precompiled
{
class VRFInfo
{
public:
    using Ptr = std::shared_ptr<VRFInfo>;
    VRFInfo(std::string const& _vrfProof, std::string const& _vrfPk, std::string const& _vrfInput)
      : m_vrfProof(_vrfProof), m_vrfPublicKey(_vrfPk), m_vrfInput(_vrfInput)
    {}
    virtual ~VRFInfo() {}

    virtual bool verifyProof();
    virtual dev::h256 getHashFromProof();
    virtual bool isValidVRFPublicKey();

    std::string const& vrfProof() const { return m_vrfProof; }
    std::string const& vrfPublicKey() const { return m_vrfPublicKey; }
    std::string const& vrfInput() const { return m_vrfInput; }

private:
    std::string m_vrfProof;
    std::string m_vrfPublicKey;
    std::string m_vrfInput;
};

struct NodeRotatingInfo
{
    using Ptr = std::shared_ptr<NodeRotatingInfo>;
    int64_t removedWorkingSealerNum;
    int64_t insertedWorkingSealerNum;
};

class WorkingSealerManagerImpl
{
public:
    using Ptr = std::shared_ptr<WorkingSealerManagerImpl>;
    WorkingSealerManagerImpl(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        dev::Address const& _origin, dev::Address const& _sender);

    virtual ~WorkingSealerManagerImpl() {}

    void createVRFInfo(std::string const& _vrfProof, std::string const& _vrfPublicKey,
        std::string const& _vrfInput);
    VRFInfo::Ptr vrfInfo() const { return m_vrfInfo; }

    virtual void rotateWorkingSealer();

protected:
    virtual void checkVRFInfos();

    // calculate the number of working sealers that need to be added and removed
    virtual NodeRotatingInfo::Ptr calNodeRotatingInfo();

    virtual std::shared_ptr<dev::h512s> selectNodesFromList(
        std::shared_ptr<dev::h512s> _nodeList, int64_t selectedNode);

private:
    bool checkPermission(dev::Address const& _origin, dev::h512s const& _allowedAccountInfoList);
    void getSealerList();
    bool shouldRotate();
    void UpdateNodeType(dev::h512 const& _node, std::string const& _nodeType);

private:
    std::shared_ptr<dev::blockverifier::ExecutiveContext> m_context;
    Address m_origin;
    Address m_sender;
    VRFInfo::Ptr m_vrfInfo;

    std::shared_ptr<dev::h512s> m_pendingSealerList;
    std::shared_ptr<dev::h512s> m_workingSealerList;

    std::shared_ptr<dev::storage::Table> m_consTable;
    dev::h256 m_proofHash;

    int64_t m_configuredEpochSealersSize;
    bool m_sealerListObtained = false;
};
}  // namespace precompiled
}  // namespace dev