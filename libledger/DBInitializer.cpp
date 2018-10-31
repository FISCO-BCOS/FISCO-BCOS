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
 * @file: DBInitializer.cpp
 * @author: yujiechen
 * @date: 2018-10-24
 */
#include "DBInitializer.h"
#include "LedgerParam.h"
#include <libdevcore/Common.h>
#include <libmptstate/MPTStateFactory.h>
#include <libstorage/LevelDBStorage.h>
#include <libstoragestate/StorageStateFactory.h>
using namespace dev;
using namespace dev::storage;
using namespace dev::blockverifier;
using namespace dev::db;
using namespace dev::eth;
using namespace dev::mptstate;
using namespace dev::executive;
using namespace dev::storagestate;

namespace dev
{
namespace ledger
{
void DBInitializer::initStorageDB()
{
    if (dev::stringCmpIgnoreCase(m_param->dbType(), "AMDB") == 0)
        initAMOPStorage();
    if (dev::stringCmpIgnoreCase(m_param->dbType(), "LevelDB") == 0)
        initLevelDBStorage();
}

/// init the storage with leveldb
void DBInitializer::initLevelDBStorage()
{
    m_storage = std::make_shared<LevelDBStorage>();
    /// open and init the levelDB
    leveldb::Options ldb_option;
    leveldb::DB* pleveldb;
    try
    {
        ldb_option.create_if_missing = true;
        LOG(DEBUG) << "open leveldb handler";
        leveldb::Status status = leveldb::DB::Open(ldb_option, m_param->baseDir(), &(pleveldb));
        std::shared_ptr<LevelDBStorage> leveldb_storage =
            std::dynamic_pointer_cast<LevelDBStorage>(m_storage);
        assert(leveldb_storage);
        std::shared_ptr<leveldb::DB> leveldb_handler = std::shared_ptr<leveldb::DB>(pleveldb);
        leveldb_storage->setDB(leveldb_handler);
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "initLevelDBStorage failed, error information:" << e.what();
        BOOST_THROW_EXCEPTION(OpenLevelDBFailed() << errinfo_comment("initLevelDBStorage failed"));
    }
}

/// TODO: init AMOP Storage
void DBInitializer::initAMOPStorage() {}

/// create ExecutiveContextFactory
void DBInitializer::createExecutiveContext(
    std::unordered_map<Address, dev::eth::PrecompiledContract> const& precompile)
{
    assert(m_storage && m_stateFactory);
    m_executiveContextFac = std::make_shared<ExecutiveContextFactory>();
    /// storage
    m_executiveContextFac->setStateStorage(m_storage);
    // mpt or storage
    m_executiveContextFac->setStateFactory(m_stateFactory);
    /// precompile
    m_executiveContextFac->setPrecompiledContract(precompile);
}

/// create stateFactory
void DBInitializer::createStateFactory()
{
    if (m_param->enableMpt())
        createMptState();
    else
        createStorageState();
}

/// TOCHECK: create the stateStorage with AMDB
void DBInitializer::createStorageState()
{
    m_stateFactory =
        std::make_shared<StorageStateFactory>(m_param->mutableGenesisParam().accountStartNonce);
}

/// create the mptState
void DBInitializer::createMptState()
{
    m_stateFactory =
        std::make_shared<MPTStateFactory>(m_param->mutableGenesisParam().accountStartNonce,
            m_param->baseDir(), m_param->mutableGenesisParam().genesisHash, WithExisting::Trust);
}

}  // namespace ledger
}  // namespace dev
