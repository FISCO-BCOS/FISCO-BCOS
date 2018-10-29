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
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : initializer for DB
 * @file: DBInitializer.h
 * @author: yujiechen
 * @date: 2018-10-24
 */
#pragma once
#include "LedgerParamInterface.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcore/OverlayDB.h>
#include <libexecutive/StateFactoryInterface.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/Storage.h>
#include <memory>
namespace dev
{
namespace ledger
{
class DBInitializer
{
public:
    DBInitializer(std::shared_ptr<LedgerParamInterface> param) : m_param(param) {}

    virtual void initDBModules(
        std::unordered_map<Address, dev::eth::PrecompiledContract> const& preCompile)
    {
        assert(m_param);
        /// init the storage DB
        initStorageDB();
        /// create state storage
        createStateFactory();
        /// create executive context
        createExecutiveContext(preCompile);
    }

    dev::storage::Storage::Ptr storage() const
    {
        assert(m_storage);
        return m_storage;
    }

    std::shared_ptr<dev::blockverifier::ExecutiveContextFactory> executiveContextFactory() const
    {
        assert(m_executiveContextFac);
        return m_executiveContextFac;
    }

protected:
    /// create storage DB(must be storage)
    ///  must be open before init
    virtual void initStorageDB();

    /// mpt && storagestate(2选1)
    /// TODO:storageStateFactory( mpt && storage 2 选1)
    /// create stateStorage (mpt or storageState options)
    virtual void createStateFactory();
    /// create ExecutiveContextFactory
    virtual void createExecutiveContext(
        std::unordered_map<Address, dev::eth::PrecompiledContract> const& precompile);

private:
    /// TODO: init AMDB storage
    void initAMDBStorage();
    /// TOTEST: init levelDB storage
    void initLevelDBStorage();
    /// TODO: create AMDB/mpt stateStorage
    void createStorageState();
    void createMptState();

private:
    std::shared_ptr<LedgerParamInterface> m_param;
    std::shared_ptr<dev::executive::StateFactoryInterface> m_stateFactory;
    dev::storage::Storage::Ptr m_storage = nullptr;
    std::shared_ptr<dev::blockverifier::ExecutiveContextFactory> m_executiveContextFac;
};
}  // namespace ledger
}  // namespace dev
