/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file MinerPrecompiled.h
 *  @author caryliao
 *  @date 20181205
 */
#include "AuthorityPrecompiled.h"
#include "libstorage/EntriesPrecompiled.h"
#include "libstorage/TableFactoryPrecompiled.h"
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;

std::string AuthorityPrecompiled::toString(ExecutiveContext::Ptr)
{
    return "Authority";
}

storage::Table::Ptr AuthorityPrecompiled::openTable(
    ExecutiveContext::Ptr context, const std::string& tableName)
{
    STORAGE_LOG(DEBUG) << "Authority open table:" << tableName;
    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
        std::dynamic_pointer_cast<TableFactoryPrecompiled>(
            context->getPrecompiled(Address(0x1001)));
    return tableFactoryPrecompiled->getmemoryTableFactory()->openTable(tableName);
}

bytes AuthorityPrecompiled::call(ExecutiveContext::Ptr context, bytesConstRef param)
{
    STORAGE_LOG(TRACE) << "this: " << this << " call Authority:" << toHex(param);

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    STORAGE_LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;
    bytes out;

    switch (func)
    {
    // insert(string,string)
    // insert(string tableName,string addr)
    case 0x06e63ff8:
    {
        std::string tableName, addr;
        abi.abiOut(data, tableName, addr);
        Table::Ptr table = openTable(context, SYS_ACCESS_TABLE);

        if (!table)
        {
            STORAGE_LOG(WARNING) << "open SYS_ACCESS_TABLE table failed.";
            out = abi.abiIn("", u256(0));
            break;
        }

        bool exist = false;
        auto entries = table->select(tableName, table->newCondition());
        if (entries.get())
        {
             for (size_t i = 0; i < entries->size(); i++)
             {
                 auto entry = entries->get(i);
                 if (!entry)
                     continue;
                 if (entry->getField(SYS_AC_FIELD_ADDRESS) == addr)
                 {
                     exist = true;
                     break;
                 }
             }
         }
         if (exist)
         {
             STORAGE_LOG(WARNING) << "Authority entry with same table name and address has existed.";
             out = abi.abiIn("", u256(0));
             break;
         }

         // do insert
	    auto entry = table->newEntry();
	    entry->setField(SYS_AC_FIELD_TABLE_NAME, tableName);
	    entry->setField(SYS_AC_FIELD_ADDRESS, addr);
	    entry->setField(SYS_AC_FIELD_ENABLENUM, boost::lexical_cast<std::string>(context->blockInfo().number + 1));
	    size_t count = table->insert(SYS_AC_FIELD_TABLE_NAME, entry);
	    out = abi.abiIn("", u256(count));

        break;
    }
    // remove(string,string)
    // remove(string tableName,string addr)
    case 0x44590a7e:
    {
        std::string tableName, addr;
        abi.abiOut(data, tableName, addr);
        Table::Ptr table = openTable(context, SYS_ACCESS_TABLE);

        if (!table)
        {
            STORAGE_LOG(WARNING) << "open SYS_ACCESS_TABLE table failed.";
            out = abi.abiIn("", u256(0));
            break;
        }

        bool exist = false;
        auto condition = table->newCondition();
        condition->EQ(SYS_AC_FIELD_ADDRESS, addr);
        auto entries = table->select(tableName, condition);
        if (entries->size() == 0u)
        {
            STORAGE_LOG(WARNING) << "Authority entry with the table name and address has not existed.";
            out = abi.abiIn("", u256(0));
        }
        else
        {
			size_t count = table->remove(tableName, condition);
            out = abi.abiIn("", count);
        }

        break;
    }
    default:
    {
        break;
    }
    }
    return out;
}

