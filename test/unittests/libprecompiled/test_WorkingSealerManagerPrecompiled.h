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
 * @brief: ut for WorkingSealerManagerPrecompiled
 * @file : test_WorkingSealerManagerPrecompiled.h
 * @author: yujiechen
 * @date: 2020-06-22
 */

#pragma once
#include "ffi_vrf.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libprecompiled/WorkingSealerManagerPrecompiled.h>
#include <libstorage/CachedStorage.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactoryFactory2.h>
#include <libutilities/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libstorage/Common.h>
#include <test/unittests/libstorage/MemoryStorage2.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::precompiled;
using namespace bcos::blockverifier;
using namespace bcos::eth;
using namespace bcos::storagestate;
using namespace bcos::storage;

namespace bcos
{
namespace test
{
class WorkingSealerManagerFixture
{
public:
    using Ptr = std::shared_ptr<WorkingSealerManagerFixture>;
    WorkingSealerManagerFixture() {}
    WorkingSealerManagerFixture(WorkingSealerManagerFixture::Ptr _fixture,
        size_t _epochSealerNum = 4, size_t _epochBlockNum = 10)
      : m_keyPair(_fixture->m_keyPair),
        sealerList(_fixture->sealerList),
        m_publicKey2KeyPair(_fixture->m_publicKey2KeyPair)
    {
        initWorkingSealerMgrFixture(_epochSealerNum, _epochBlockNum);
    }

    void initWorkingSealerManagerFixture(size_t _sealersSize = 4, size_t _epochSealerNum = 4,
        size_t _epochBlockNum = 10, bool _needFakeSealerList = true)
    {
        if (_needFakeSealerList)
        {
            // init VRF public and private keys
            m_keyPair = KeyPair::create();

            // add the node into the sealerList
            sealerList.push_back(m_keyPair.pub());
            m_publicKey2KeyPair[m_keyPair.pub()] = m_keyPair;
            // generate sealerList
            for (size_t i = 1; i < _sealersSize; i++)
            {
                auto keyPair = KeyPair::create();
                sealerList.push_back(keyPair.pub());
                m_publicKey2KeyPair[keyPair.pub()] = keyPair;
            }
        }
        initWorkingSealerMgrFixture(_epochSealerNum, _epochBlockNum);
    }

    void initWorkingSealerMgrFixture(size_t _epochSealerNum = 4, size_t _epochBlockNum = 10)
    {
        context = std::make_shared<ExecutiveContext>();
        workingSealerManagerPrecompiled = std::make_shared<WorkingSealerManagerPrecompiled>();
        initContext();

        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<bcos::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        workingSealerManagerPrecompiled->setPrecompiledExecResultFactory(
            precompiledExecResultFactory);

        // init VRF public and private keys
        auto vrfKeyPair = generateVRFKeyPair(m_keyPair);
        vrfPrivateKey = vrfKeyPair.first;
        vrfPublicKey = vrfKeyPair.second;

        // add the node into the sealerList
        addSealerList(sealerList);

        std::sort(sealerList.begin(), sealerList.end());
        // init epoch_sealer_num
        setSystemConfigByKey(
            SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, boost::lexical_cast<std::string>(_epochSealerNum));
        setSystemConfigByKey(
            SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, boost::lexical_cast<std::string>(_epochBlockNum));
        // init workingSealer
        initWorkingSealers(_epochSealerNum);
    }

    void initContext()
    {
        // init storage and tables
        ExecutiveContextFactory factory;
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory2>();
        auto memStorage = std::make_shared<MemoryStorage2>();
        cachedStorage = std::make_shared<CachedStorage>();
        cachedStorage->setBackend(memStorage);
        tableFactoryFactory->setStorage(cachedStorage);

        factory.setStateStorage(cachedStorage);
        factory.setStateFactory(storageStateFactory);
        factory.setTableFactoryFactory(tableFactoryFactory);
        BlockInfo blockInfo;
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        factory.initExecutiveContext(blockInfo, h256(0), context);
        memoryTableFactory = context->getMemoryTableFactory();
    }

    void initWorkingSealers(size_t _epochSealerNum)
    {
        auto workingSealersSize = std::min(_epochSealerNum, sealerList.size());
        updateNodeListType(sealerList, workingSealersSize, NODE_TYPE_WORKING_SEALER);
    }

    std::string getSystemConfigByKey(std::string const& _key)
    {
        return getSystemConfigAndEnableNumByKey(_key).first;
    }

    std::pair<std::string, bcos::eth::BlockNumber> getSystemConfigAndEnableNumByKey(
        std::string const& _key)
    {
        auto table = openTable(context, SYS_CONFIG);
        auto valueInfo =
            bcos::precompiled::getSysteConfigByKey(table, _key, context->blockInfo().number + 1);
        return *valueInfo;
    }

    void setSystemConfigByKey(std::string const& _key, std::string const& _value)
    {
        auto table = openTable(context, SYS_CONFIG);
        auto condition = table->newCondition();
        auto entries = table->select(_key, condition);
        auto entry = table->newEntry();
        entry->setField(SYSTEM_CONFIG_KEY, _key);
        entry->setField(SYSTEM_CONFIG_VALUE, _value);
        entry->setField(
            SYSTEM_CONFIG_ENABLENUM, boost::lexical_cast<std::string>(context->blockInfo().number));
        if (!entries || entries->size() == 0)
        {
            table->insert(_key, entry);
        }
        else
        {
            table->update(_key, entry, condition);
        }
    }

    void updateNodeListType(
        bcos::h512s const& _nodeList, size_t _size, std::string const& _nodeType)
    {
        auto updateSize = std::min(_nodeList.size(), _size);
        auto table = openTable(context, SYS_CONSENSUS);
        if (!table)
        {
            return;
        }
        for (size_t i = 0; i < updateSize; i++)
        {
            auto nodeId = _nodeList[i];
            auto condition = table->newCondition();
            condition->EQ(NODE_KEY_NODEID, *toHexString(nodeId));
            // select the entries
            auto entries = table->select(PRI_KEY, condition);
            // select succ
            auto entry = table->newEntry();
            entry->setField(NODE_TYPE, _nodeType);
            entry->setField(PRI_COLUMN, PRI_KEY);
            entry->setField(
                NODE_KEY_ENABLENUM, boost::lexical_cast<std::string>(context->blockInfo().number));
            // select failed
            if (!entries || entries->size() == 0)
            {
                entry->setField(NODE_KEY_NODEID, *toHexString(nodeId));
                table->insert(PRI_KEY, entry);
            }
            else
            {
                table->update(PRI_KEY, entry, condition);
            }
        }
    }

    void addSealerList(bcos::h512s const& _sealerList)
    {
        updateNodeListType(_sealerList, _sealerList.size(), NODE_TYPE_SEALER);
    }

    std::pair<std::string, std::string> generateVRFKeyPair(KeyPair const& _keyPair)
    {
        auto vrfPrivateKey = *toHexString(bytesConstRef{_keyPair.secret().data(), 32});
        // generate vrfPublicKey
        auto vrfPublicKey = curve25519_vrf_generate_key_pair(vrfPrivateKey.c_str());
        return std::make_pair(vrfPrivateKey, vrfPublicKey);
    }

    std::pair<std::string, std::string> generateVRFProof(
        ExecutiveContext::Ptr _executiveContext, std::string const& _privateKey)
    {
        // get block Hash
        std::string hash = *toHexString(_executiveContext->blockInfo().hash);
        auto vrfProof = curve25519_vrf_proof(_privateKey.c_str(), hash.c_str());
        return std::make_pair(hash, vrfProof);
    }

    void getSealerList()
    {
        auto table = openTable(context, SYS_CONSENSUS);
        if (!table)
        {
            return;
        }
        auto blockNumber = context->blockInfo().number + 1;
        m_pendingSealerList = getNodeListByType(table, blockNumber, NODE_TYPE_SEALER);
        m_workingSealerList = getNodeListByType(table, blockNumber, NODE_TYPE_WORKING_SEALER);
        m_observerList = getNodeListByType(table, blockNumber, NODE_TYPE_OBSERVER);
        std::sort(m_pendingSealerList.begin(), m_pendingSealerList.end());
        std::sort(m_workingSealerList.begin(), m_workingSealerList.end());
        std::sort(m_observerList.begin(), m_observerList.end());
    }

    ~WorkingSealerManagerFixture() {}

    ExecutiveContext::Ptr context;
    WorkingSealerManagerPrecompiled::Ptr workingSealerManagerPrecompiled;

    std::string vrfPublicKey;
    std::string vrfPrivateKey;
    KeyPair m_keyPair;

    bcos::h512s sealerList;
    std::map<bcos::h512, bcos::KeyPair> m_publicKey2KeyPair;
    bcos::h512s m_pendingSealerList;
    bcos::h512s m_workingSealerList;
    bcos::h512s m_observerList;

    TableFactory::Ptr memoryTableFactory;
    CachedStorage::Ptr cachedStorage;
};
}  // namespace test
}  // namespace bcos
