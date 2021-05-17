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
 * (c) 2016-2019 fisco-dev contributors.
 */
/** @file StashDBInitializer.cpp
 *  @author wesleywang
 *  @date 2021-5-10
 */
#include "StashDBInitializer.h"
#include "StashSQLBasicAccess.h"
#include "StashStorage.h"
#include "libstorage/SQLConnectionPool.h"
#include <libledger/LedgerParam.h>


using namespace std;
using namespace dev;
using namespace dev::storage;
using namespace dev::blockverifier;
using namespace dev::db;
using namespace dev::ledger;
using namespace dev::eth;
using namespace dev::executive;

StashDBInitializer::StashDBInitializer() {}

dev::storage::Storage::Ptr dev::ledger::createStashStorage(
    std::shared_ptr<LedgerParamInterface> _param,
    std::function<void(std::exception& e)> _fatalHandler)
{
    std::cout << "ConnectionPoolConfig connectionConfig build ..." << std::endl;
    ConnectionPoolConfig connectionConfig{_param->mutableStorageParam().dbType,
        _param->mutableStorageParam().dbIP, _param->mutableStorageParam().dbPort,
        _param->mutableStorageParam().dbUsername, _param->mutableStorageParam().dbPasswd,
        _param->mutableStorageParam().dbName, _param->mutableStorageParam().dbCharset,
        _param->mutableStorageParam().initConnections,
        _param->mutableStorageParam().maxConnections};
    auto stashStorage = std::make_shared<StashStorage>();

    std::cout << "ConnectionPoolConfig connectionConfig build finish " << std::endl;

    auto sqlconnpool = std::make_shared<SQLConnectionPool>();
    sqlconnpool->InitConnectionPool(connectionConfig);

    std::cout << "ConnectionPoolConfig InitConnectionPool finish " << std::endl;
    auto sqlAccess = std::make_shared<StashSQLBasicAccess>();
    stashStorage->SetStashSqlAccess(sqlAccess);
    stashStorage->setStashConnPool(sqlconnpool);

    stashStorage->setFatalHandler(_fatalHandler);
    stashStorage->setMaxRetry(_param->mutableStorageParam().maxRetry);

    return stashStorage;
}
