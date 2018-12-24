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
#include <libsecurity/EncryptedLevelDB.h>
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
    DBInitializer_LOG(DEBUG) << "[#initStorageDB]" << std::endl;
    /// TODO: implement AMOP storage
    if (dev::stringCmpIgnoreCase(m_param->mutableStorageParam().type, "LevelDB") != 0)
    {
        DBInitializer_LOG(ERROR) << "Unsupported dbType, current version only supports levelDB"
                                 << std::endl;
    }
    initLevelDBStorage();
}

/// init the storage with leveldb
void DBInitializer::initLevelDBStorage()
{
    DBInitializer_LOG(INFO) << "[#initStorageDB] [#initLevelDBStorage] ..." << std::endl;
    /// open and init the levelDB
    leveldb::Options ldb_option;
    dev::db::BasicLevelDB* pleveldb = nullptr;
    try
    {
        boost::filesystem::create_directories(m_param->mutableStorageParam().path);
        ldb_option.create_if_missing = true;
        ldb_option.max_open_files = 100;

        leveldb::Status status;

        if (g_BCOSConfig.diskEncryption.enable)
        {
            // Use disk encryption
            DBInitializer_LOG(DEBUG)
                << "[#initStorageDB] [#initLevelDBStorage]: open leveldb handler" << std::endl;
            status = EncryptedLevelDB::Open(ldb_option, m_param->mutableStorageParam().path,
                &(pleveldb), g_BCOSConfig.diskEncryption.cipherDataKey);
        }
        else
        {
            // Not to use disk encryption
            DBInitializer_LOG(DEBUG)
                << "[#initStorageDB] [#initLevelDBStorage]: open encrypted leveldb handler"
                << std::endl;
            status =
                BasicLevelDB::Open(ldb_option, m_param->mutableStorageParam().path, &(pleveldb));
        }

        if (!status.ok())
        {
            DBInitializer_LOG(ERROR) << "[#initStorageDB] [openLevelDBStorage failed]" << std::endl;
            return;
        }
        DBInitializer_LOG(DEBUG) << "[#initStorageDB] [#initLevelDBStorage] [status]: "
                                 << status.ok() << std::endl;
        std::shared_ptr<LevelDBStorage> leveldb_storage = std::make_shared<LevelDBStorage>();
        assert(leveldb_storage);
        std::shared_ptr<dev::db::BasicLevelDB> leveldb_handler =
            std::shared_ptr<dev::db::BasicLevelDB>(pleveldb);
        leveldb_storage->setDB(leveldb_handler);
        m_storage = leveldb_storage;
    }
    catch (std::exception& e)
    {
        DBInitializer_LOG(ERROR) << "[#initLevelDBStorage] initLevelDBStorage failed, [EINFO]: "
                                 << boost::diagnostic_information(e);
        BOOST_THROW_EXCEPTION(OpenLevelDBFailed() << errinfo_comment("initLevelDBStorage failed"));
    }
}

/// TODO: init AMOP Storage
void DBInitializer::initAMOPStorage()
{
    DBInitializer_LOG(INFO) << "[#initAMOPStorage/Unimplemented] ..." << std::endl;
}

/// create ExecutiveContextFactory
void DBInitializer::createExecutiveContext()
{
    if (!m_storage || !m_stateFactory)
    {
        DBInitializer_LOG(ERROR)
            << "[#createExecutiveContext Failed for storage has not been initialized]" << std::endl;
        return;
    }
    DBInitializer_LOG(DEBUG) << "[#createExecutiveContext]" << std::endl;
    m_executiveContextFac = std::make_shared<ExecutiveContextFactory>();
    /// storage
    m_executiveContextFac->setStateStorage(m_storage);
    // mpt or storage
    m_executiveContextFac->setStateFactory(m_stateFactory);
    DBInitializer_LOG(DEBUG) << "[#createExecutiveContext SUCC]" << std::endl;
}

/// create stateFactory
void DBInitializer::createStateFactory(dev::h256 const& genesisHash)
{
    DBInitializer_LOG(DEBUG) << "[#createStateFactory]" << std::endl;
    if (dev::stringCmpIgnoreCase(m_param->mutableStateParam().type, "mpt") == 0)
        createMptState(genesisHash);
    else if (dev::stringCmpIgnoreCase(m_param->mutableStateParam().type, "storage") ==
             0)  /// default is storage state
        createStorageState();
    else
    {
        DBInitializer_LOG(WARNING)
            << "[#createStateFactory] only support storage and mpt now, create storage by default"
            << std::endl;
        createStorageState();
    }
    DBInitializer_LOG(DEBUG) << "[#createStateFactory SUCC]" << std::endl;
}

/// TOCHECK: create the stateStorage with AMDB
void DBInitializer::createStorageState()
{
    DBInitializer_LOG(DEBUG) << "[#createStateFactory] [#createStorageState]" << std::endl;
    m_stateFactory = std::make_shared<StorageStateFactory>(u256(0x0));
    DBInitializer_LOG(DEBUG) << "[#createStateFactory] [#createStorageState SUCC]" << std::endl;
}

/// create the mptState
void DBInitializer::createMptState(dev::h256 const& genesisHash)
{
    DBInitializer_LOG(DEBUG) << "[#createStateFactory] [#createMptState]" << std::endl;

    m_stateFactory = std::make_shared<MPTStateFactory>(
        u256(0x0), m_param->baseDir(), genesisHash, WithExisting::Trust);
    DBInitializer_LOG(DEBUG) << "[#createStateFactory] [#createMptState SUCC]" << std::endl;
}

}  // namespace ledger
}  // namespace dev
