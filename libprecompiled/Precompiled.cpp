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
/** @file Precompiled.cpp
 *  @author xingqiangbai
 *  @date 20190115
 */

#include "Precompiled.h"
#include "libstorage/Table.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/CryptoInterface.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <libstorage/MemoryTableFactory.h>

using namespace bcos;
using namespace precompiled;
using namespace bcos::blockverifier;

// global function selector cache
static tbb::concurrent_unordered_map<std::string, uint32_t> s_name2SelectCache;

uint32_t Precompiled::getFuncSelector(std::string const& _functionName)
{
    // global function selector cache
    if (s_name2SelectCache.count(_functionName))
    {
        return s_name2SelectCache[_functionName];
    }
    auto selector = getFuncSelectorByFunctionName(_functionName);
    s_name2SelectCache.insert(std::make_pair(_functionName, selector));
    return selector;
}

storage::Table::Ptr Precompiled::createTable(
    std::shared_ptr<bcos::blockverifier::ExecutiveContext> context, const std::string& tableName,
    const std::string& keyField, const std::string& valueField, Address const& origin)
{
    return context->getMemoryTableFactory()->createTable(
        tableName, keyField, valueField, false, origin, true);
}

bool Precompiled::checkAuthority(std::shared_ptr<bcos::blockverifier::ExecutiveContext> _context,
    Address const& _origin, Address const& _contract)
{
    auto tableName = getContractTableName(_contract);
    auto table = openTable(_context, tableName);
    if (table)
    {
        return table->checkAuthority(_origin);
    }
    return true;
}

uint64_t Precompiled::getEntriesCapacity(
    std::shared_ptr<bcos::storage::Entries const> _entries) const
{
    int64_t totalCapacity = 0;
    int64_t entriesSize = _entries->size();
    for (int64_t i = 0; i < entriesSize; i++)
    {
        totalCapacity += _entries->get(i)->capacity();
    }
    return totalCapacity;
}

// for UT
void bcos::precompiled::clearName2SelectCache()
{
    s_name2SelectCache.clear();
}
