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
/** @file StashDBInitializer.h
 *  @author wesleywang
 *  @date 2021-5-10
 */
#pragma once
#include <libledger/DBInitializer.h>
#include <libledger/LedgerParamInterface.h>
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


namespace dev
{
namespace storage
{
class BasicRocksDB;
class BinLogHandler;
struct ConnectionPoolConfig;
}  // namespace storage

namespace ledger
{
class StashDBInitializer : public DBInitializer
{
public:
    StashDBInitializer();
    virtual ~StashDBInitializer(){};
};
dev::storage::Storage::Ptr createStashStorage(
    std::shared_ptr<::dev::ledger::LedgerParamInterface> _param,
    std::function<void(std::exception& e)> _fatalHandler);
}
}

