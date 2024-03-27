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
/** @file StashSQLBasicAccess.h
 *  @author wesleywang
 *  @date 2021-5-10
 */
#include <libledger/DBInitializer.h>
#include <libledger/LedgerParam.h>
#include "libdevcrypto/CryptoInterface.h"
#include "libstorage/SQLConnectionPool.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <libprecompiled/Precompiled.h>
#include <libstoragestate/StorageStateFactory.h>
#include <boost/lexical_cast.hpp>
#include "StashDBInitializer.h"
#include "StashStorage.h"


using namespace std;
using namespace dev;
using namespace dev::storage;
using namespace dev::blockverifier;
using namespace dev::db;
using namespace dev::ledger;
using namespace dev::eth;
using namespace dev::executive;
using namespace dev::storagestate;

dev::storage::Storage::Ptr dev::ledger::createStashStorage(
    std::shared_ptr<LedgerParamInterface> _param,
    std::function<void(std::exception& e)> _fatalHandler)
{
    std::cout << "ConnectionPoolConfig connectionConfig" << std::endl;
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
    auto sqlAccess = std::make_shared<SQLBasicAccess>();
    stashStorage->SetSqlAccess(sqlAccess);
    stashStorage->setConnPool(sqlconnpool);

    stashStorage->setFatalHandler(_fatalHandler);
    stashStorage->setMaxRetry(_param->mutableStorageParam().maxRetry);

    return stashStorage;
}
