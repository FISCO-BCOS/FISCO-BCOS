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
#include <libchannelserver/ChannelRPCServer.h>
#include <libdevcore/BasicLevelDB.h>
#include <libdevcore/OverlayDB.h>
#include <libexecutive/StateFactoryInterface.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/MemoryTableFactory2.h>
#include <libstorage/Storage.h>
#include <memory>

#define DBInitializer_LOG(LEVEL) LOG(LEVEL) << "[DBINITIALIZER] "
namespace dev
{
namespace ledger
{
class DBInitializer
{
public:
    DBInitializer(std::shared_ptr<LedgerParamInterface> param) : m_param(param) {}
    /// create storage DB(must be storage)
    ///  must be open before init
    virtual void initStorageDB();
    virtual ~DBInitializer() = default;
    virtual void initState(dev::h256 const& genesisHash)
    {
        if (!m_param)
            return;
        /// create state storage
        createStateFactory(genesisHash);
        /// create executive context
        createExecutiveContext();
    }

    dev::storage::TableFactoryFactory::Ptr tableFactoryFactory() { return m_tableFactoryFactory; };
    dev::storage::Storage::Ptr storage() const { return m_storage; }
    std::shared_ptr<dev::executive::StateFactoryInterface> stateFactory() { return m_stateFactory; }
    std::shared_ptr<dev::blockverifier::ExecutiveContextFactory> executiveContextFactory() const
    {
        return m_executiveContextFactory;
    }

    virtual void setChannelRPCServer(ChannelRPCServer::Ptr channelRPCServer)
    {
        m_channelRPCServer = channelRPCServer;
    }

protected:
    /// create stateStorage (mpt or storageState options)
    virtual void createStateFactory(dev::h256 const& genesisHash);
    /// create ExecutiveContextFactory
    virtual void createExecutiveContext();

private:
    void initLevelDBStorage();
    // below use MemoryTableFactory2
    void initSQLStorage();
    void initTableFactory2(dev::storage::Storage::Ptr _backend);
    void initLevelDBStorage2();
    void initRocksDBStorage();

    void createStorageState();
    void createMptState(dev::h256 const& genesisHash);

    void initZdbStorage();


private:
    std::shared_ptr<LedgerParamInterface> m_param;
    std::shared_ptr<dev::executive::StateFactoryInterface> m_stateFactory;
    dev::storage::Storage::Ptr m_storage = nullptr;
    std::shared_ptr<dev::blockverifier::ExecutiveContextFactory> m_executiveContextFactory;
    std::shared_ptr<ChannelRPCServer> m_channelRPCServer;

    dev::storage::TableFactoryFactory::Ptr m_tableFactoryFactory;
};
}  // namespace ledger
}  // namespace dev
