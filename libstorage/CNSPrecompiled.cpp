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
/** @file CNSPrecompiled.cpp
 *  @author chaychen
 *  @date 20181119
 */
#include "CNSPrecompiled.h"
#include "libstorage/EntriesPrecompiled.h"
#include "libstorage/TableFactoryPrecompiled.h"
#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;

std::string CNSPrecompiled::toString(ExecutiveContext::Ptr)
{
    return "CNS";
}

Table::Ptr CNSPrecompiled::openTable(ExecutiveContext::Ptr context, const std::string& tableName)
{
    STORAGE_LOG(DEBUG) << "CNS open table:" << tableName;
    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
        std::dynamic_pointer_cast<TableFactoryPrecompiled>(
            context->getPrecompiled(Address(0x1001)));
    return tableFactoryPrecompiled->getmemoryTableFactory()->openTable(tableName);
}

bytes CNSPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    STORAGE_LOG(TRACE) << "this: " << this << " call CNS:" << toHex(param);

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    STORAGE_LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;
    bytes out;

    switch (func)
    {
    case 0xa216464b:
    {
        // insert(string,string,string,string)
        // insert(name, version, address, abi), 4 fields in table, the key of table is name field
        std::string contractName, contractVersion, contractAddress, contractAbi;
        abi.abiOut(data, contractName, contractVersion, contractAddress, contractAbi);
        Table::Ptr table = openTable(context, SYS_CNS);

        // check exist or not
        bool exist = false;
        auto entries = table->select(contractName, table->newCondition());
        if (entries.get())
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                auto entry = entries->get(i);
                if (!entry)
                    continue;
                if (entry->getField(SYS_CNS_FIELD_VERSION) == contractVersion)
                {
                    exist = true;
                    break;
                }
            }
        }
        if (exist)
        {
            STORAGE_LOG(WARNING) << "CNS entry with same name and version has existed.";
            out = abi.abiIn("", u256(0));
            break;
        }

        // do insert
        auto entry = table->newEntry();
        entry->setField(SYS_CNS_FIELD_NAME, contractName);
        entry->setField(SYS_CNS_FIELD_VERSION, contractVersion);
        entry->setField(SYS_CNS_FIELD_ADDRESS, contractAddress);
        entry->setField(SYS_CNS_FIELD_ABI, contractAbi);
        size_t count = table->insert(contractName, entry);
        out = abi.abiIn("", u256(count));

        break;
    }
    case 0x819a3d62:
    {
        // selectByName(string) returns(string)
        // Cursor is not considered.
        std::string contractName;
        abi.abiOut(data, contractName);
        Table::Ptr table = openTable(context, SYS_CNS);

        json_spirit::Array CNSInfos;
        auto entries = table->select(contractName, table->newCondition());
        if (entries.get())
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                auto entry = entries->get(i);
                if (!entry)
                    continue;
                json_spirit::Object CNSInfo;
                CNSInfo.push_back(json_spirit::Pair(SYS_CNS_FIELD_NAME, contractName));
                CNSInfo.push_back(json_spirit::Pair(
                    SYS_CNS_FIELD_VERSION, entry->getField(SYS_CNS_FIELD_VERSION)));
                CNSInfo.push_back(json_spirit::Pair(
                    SYS_CNS_FIELD_ADDRESS, entry->getField(SYS_CNS_FIELD_ADDRESS)));
                CNSInfo.push_back(
                    json_spirit::Pair(SYS_CNS_FIELD_ABI, entry->getField(SYS_CNS_FIELD_ABI)));
                CNSInfos.push_back(CNSInfo);
            }
        }
        json_spirit::Value value(CNSInfos);
        std::string str = json_spirit::write_string(value, true);
        out = abi.abiIn("", str);

        break;
    }
    case 0x897f0251:
    {
        // selectByNameAndVersion(string,string) returns(string)
        std::string contractName, contractVersion;
        abi.abiOut(data, contractName, contractVersion);
        Table::Ptr table = openTable(context, SYS_CNS);

        json_spirit::Array CNSInfos;
        auto entries = table->select(contractName, table->newCondition());
        if (entries.get())
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                auto entry = entries->get(i);
                if (contractVersion == entry->getField(SYS_CNS_FIELD_VERSION))
                {
                    json_spirit::Object CNSInfo;
                    CNSInfo.push_back(json_spirit::Pair(SYS_CNS_FIELD_NAME, contractName));
                    CNSInfo.push_back(json_spirit::Pair(
                        SYS_CNS_FIELD_VERSION, entry->getField(SYS_CNS_FIELD_VERSION)));
                    CNSInfo.push_back(json_spirit::Pair(
                        SYS_CNS_FIELD_ADDRESS, entry->getField(SYS_CNS_FIELD_ADDRESS)));
                    CNSInfo.push_back(
                        json_spirit::Pair(SYS_CNS_FIELD_ABI, entry->getField(SYS_CNS_FIELD_ABI)));
                    CNSInfos.push_back(CNSInfo);
                    // Only one
                    break;
                }
            }
        }
        json_spirit::Value value(CNSInfos);
        std::string str = json_spirit::write_string(value, true);
        out = abi.abiIn("", str);

        break;
    }
    default:
    {
        STORAGE_LOG(ERROR) << "error func:" << std::hex << func;
        break;
    }
    }

    return out;
}
