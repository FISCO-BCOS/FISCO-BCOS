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
#include <libmptstate/State.h>
using namespace dev::storage;
using namespace dev::blockverifier;
using namespace dev::db;
using namespace dev::eth;

namespace dev
{
namespace ledger
{
/// create stateStorage
void DBInitializer::createStateStorage()
{
    m_storage = std::shared_ptr<Storage>();
}

/// init the DB
void DBInitializer::initStorageDB()
{
    if (dev::stringCmpIgnoreCase(m_param->dbType(), "AMDB") == 0)
        initAMDB();
    if (dev::stringCmpIgnoreCase(m_param->dbType(), "LevelDB") == 0)
        initLevelDB();
}

/// TODO: init AMDB
void DBInitializer::initAMDB() {}

/// open levelDB for MPTState
void DBInitializer::openMPTStateDB()
{
    m_mptStateDB = State::openDB(
        boost::filesystem::path(m_param->baseDir()), m_param->mutableGenesisParam().genesisHash);
}
/// create ExecutiveContextFactory
void DBInitializer::createExecutiveContext(
    std::unordered_map<Address, dev::eth::PrecompiledContract> const& preCompile)
{
    assert(m_storage && m_stateFactory);
    m_executiveContextFac = std::make_shared<ExecutiveContextFactory>();
    m_executiveContextFac->setStateStorage(m_storage);
    m_executiveContextFac->setStateFactory(m_stateFactory);
    m_executiveContextFac->setPrecompiledContract(preCompile);
}

/// TODO: create stateFactory
void DBInitializer::createStateFactory()
{
    /// m_stateFactory = std::make_shared<StateFactoryInterface>();
}

/// create memoryTableFactory
void DBInitializer::createMemoryTable()
{
    assert(m_storage);
    m_memoryTableFac = std::make_shared<MemoryTableFactory>();
    m_memoryTableFac->setStateStorage(m_storage);
}

}  // namespace ledger
}  // namespace dev
