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
#include "Common.h"
#include "libstorage/Table.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/Hash.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <libstorage/MemoryTableFactory.h>

using namespace dev;
using namespace precompiled;
using namespace dev::blockverifier;

uint32_t Precompiled::getFuncSelector(std::string const& _functionName)
{
    uint32_t func = *(uint32_t*)(sha3(_functionName).ref().cropped(0, 4).data());
    return ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) | ((func & 0x00FF0000) >> 8) |
           ((func & 0xFF000000) >> 24);
}

storage::Table::Ptr Precompiled::openTable(
    ExecutiveContext::Ptr context, const std::string& tableName)
{
    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
        std::dynamic_pointer_cast<TableFactoryPrecompiled>(
            context->getPrecompiled(Address(0x1001)));
    return tableFactoryPrecompiled->getMemoryTableFactory()->openTable(tableName);
}

storage::Table::Ptr Precompiled::createTable(
    std::shared_ptr<dev::blockverifier::ExecutiveContext> context, const std::string& tableName,
    const std::string& keyField, const std::string& valueField, Address const& origin)
{
    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
        std::dynamic_pointer_cast<TableFactoryPrecompiled>(
            context->getPrecompiled(Address(0x1001)));
    return tableFactoryPrecompiled->getMemoryTableFactory()->createTable(
        tableName, keyField, valueField, true, origin, true);
}

bool Precompiled::checkAuthority(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
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
