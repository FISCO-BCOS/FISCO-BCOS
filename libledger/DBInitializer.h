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
#include "libstorage/ScalableStorage.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libchannelserver/ChannelRPCServer.h>
#include <libexecutive/StateFactoryInterface.h>
#include <libmptstate/OverlayDB.h>
#include <libstorage/CachedStorage.h>
#include <libstorage/MemoryTableFactory.h>
#include <libstorage/MemoryTableFactory2.h>
#include <libstorage/Storage.h>
#include <memory>

#define DBInitializer_LOG(LEVEL) LOG(LEVEL) << "[DBINITIALIZER] "
namespace bcos
{
namespace storage
{
class BasicRocksDB;
class BinLogHandler;
struct ConnectionPoolConfig;
}  // namespace storage

namespace ledger
{
class DBInitializer
{
public:
    DBInitializer(std::shared_ptr<LedgerParamInterface> param, bcos::GROUP_ID _groupID)
      : m_groupID(_groupID), m_param(param)
    {}
    /// create storage DB(must be storage)
    ///  must be open before init
    virtual void initStorageDB();
    virtual ~DBInitializer()
    {
        if (m_storage)
        {
            m_storage->stop();
        }
        DBInitializer_LOG(INFO) << LOG_DESC("storage stopped");
    };
    virtual void initState(bcos::h256 const& genesisHash)
    {
        if (!m_param)
            return;
        /// create state storage
        createStateFactory(genesisHash);
        /// create executive context
        createExecutiveContext();
    }

    bcos::storage::TableFactoryFactory::Ptr tableFactoryFactory() { return m_tableFactoryFactory; };
    bcos::storage::Storage::Ptr storage() const { return m_storage; }
    std::shared_ptr<bcos::executive::StateFactoryInterface> stateFactory()
    {
        return m_stateFactory;
    }
    std::shared_ptr<bcos::blockverifier::ExecutiveContextFactory> executiveContextFactory() const
    {
        return m_executiveContextFactory;
    }

    virtual void setChannelRPCServer(ChannelRPCServer::Ptr channelRPCServer)
    {
        m_channelRPCServer = channelRPCServer;
    }

    void setSyncNumForCachedStorage(int64_t const& _syncNum);

protected:
    bcos::GROUP_ID m_groupID = 0;
    /// create stateStorage (mpt or storageState options)
    virtual void createStateFactory(bcos::h256 const& genesisHash);
    /// create ExecutiveContextFactory
    virtual void createExecutiveContext();

    void unsupportedFeatures(std::string const& desc);

private:
    void initLevelDBStorage();
    // below use MemoryTableFactory2
    bcos::storage::Storage::Ptr initSQLStorage();
    void initTableFactory2(
        bcos::storage::Storage::Ptr _backend, std::shared_ptr<LedgerParamInterface> _param);
    bcos::storage::Storage::Ptr initRocksDBStorage(std::shared_ptr<LedgerParamInterface> _param);
    bcos::storage::Storage::Ptr initScalableStorage(std::shared_ptr<LedgerParamInterface> _param);
    void createStorageState();

    bcos::storage::Storage::Ptr initZdbStorage();
    void recoverFromBinaryLog(std::shared_ptr<bcos::storage::BinLogHandler> _binaryLogger,
        bcos::storage::Storage::Ptr _storage);

    void setRemoteBlockNumber(std::shared_ptr<bcos::storage::ScalableStorage> scalableStorage,
        const std::string& blocksDBPath);

    std::shared_ptr<LedgerParamInterface> m_param;
    std::shared_ptr<bcos::executive::StateFactoryInterface> m_stateFactory;
    bcos::storage::Storage::Ptr m_storage = nullptr;
    std::shared_ptr<bcos::blockverifier::ExecutiveContextFactory> m_executiveContextFactory;
    std::shared_ptr<ChannelRPCServer> m_channelRPCServer;

    bcos::storage::TableFactoryFactory::Ptr m_tableFactoryFactory;
    std::shared_ptr<bcos::storage::CachedStorage> m_cacheStorage = nullptr;
};
int64_t getBlockNumberFromStorage(bcos::storage::Storage::Ptr _storage);
bcos::storage::Storage::Ptr createRocksDBStorage(
    const std::string& _dbPath, const bytes& _encryptKey, bool _disableWAL, bool _enableCache);
bcos::storage::Storage::Ptr createSQLStorage(std::shared_ptr<LedgerParamInterface> _param,
    std::shared_ptr<ChannelRPCServer> _channelRPCServer,
    std::function<void(std::exception& e)> _fatalHandler);
bcos::storage::Storage::Ptr createZdbStorage(std::shared_ptr<LedgerParamInterface> _param,
    std::function<void(std::exception& e)> _fatalHandler);
}  // namespace ledger
}  // namespace bcos
