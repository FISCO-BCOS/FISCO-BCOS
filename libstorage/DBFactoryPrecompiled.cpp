#include "DBFactoryPrecompiled.h"
#include "DBPrecompiled.h"
#include "Common.h"
#include "MemoryDB.h"
#include <libdevcore/Hash.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace dev;
using namespace dev::precompiled;
using namespace std;

void DBFactoryPrecompiled::beforeBlock(shared_ptr<PrecompiledContext> context) {}

void DBFactoryPrecompiled::afterBlock(shared_ptr<PrecompiledContext> context, bool commit)
{
    if (!commit)
    {
        LOG(DEBUG) << "Clear _name2Table: " << _name2Table.size();
        _name2Table.clear();
        return;
    }

    LOG(DEBUG) << "Submiting DBPrecompiled";

    //汇总所有表的数据并提交
    vector<dev::storage::TableData::Ptr> datas;

    for (auto dbIt : _name2Table)
    {
        DBPrecompiled::Ptr dbPrecompiled =
            dynamic_pointer_cast<DBPrecompiled>(context->getPrecompiled(dbIt.second));

        dev::storage::TableData::Ptr tableData = make_shared<dev::storage::TableData>();
        tableData->tableName = dbIt.first;

        bool dirtyTable = false;
        for (auto it : *(dbPrecompiled->getDB()->data()))
        {
            //至少有一个dirty的数据，才把全表提交给Storage
            tableData->data.insert(make_pair(it.first, it.second));

            if (it.second->dirty())
            {
                dirtyTable = true;
            }
        }

        if (!tableData->data.empty() && dirtyTable)
        {
            datas.push_back(tableData);
        }
    }

    LOG(DEBUG) << "Total: " << datas.size() << " key";
    if (!datas.empty())
    {
        if (_hash == h256())
        {
            hash(context);
        }
        LOG(DEBUG) << "Submit data:" << datas.size() << " hash:" << _hash;
        _memoryDBFactory->stateStorage()->commit(context->blockInfo().hash,
                                                 context->blockInfo().number.convert_to<int>(), datas, context->blockInfo().hash);
    }

    _name2Table.clear();
}

string DBFactoryPrecompiled::toString(shared_ptr<PrecompiledContext>)
{
    return "DBFactory";
}

bytes DBFactoryPrecompiled::call(shared_ptr<PrecompiledContext> context, bytesConstRef param)
{
    LOG(TRACE) << "this: " << this << " call DBFactory:" << toHex(param);

    //解析出函数名
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    LOG(DEBUG) << "func:" << hex << func;

    dev::eth::ContractABI abi;
    bytes out;

    switch (func)
    {
    case 0xc184e0ff: // openDB(string)
    case 0xf23f63c9:
    { // openTable(string)
        string tableName;
        abi.abiOut(data, tableName);

        LOG(DEBUG) << "DBFactory open table:" << tableName;

        Address address = openTable(context, tableName);
        out = abi.abiIn("", address);
        break;
    }
    case 0x56004b6a:
    { // createTable(string,string,string)
        string tableName;
        string keyName;
        string fieldNames;

        abi.abiOut(data, tableName, keyName, fieldNames);
        LOG(DEBUG) << "DBFactory call createTable:" << tableName;
        vector<string> fieldNameList;
        boost::split(fieldNameList, fieldNames, boost::is_any_of(","));
        for (auto &str : fieldNameList)
            boost::trim(str);
        string valueFiled = boost::join(fieldNameList, ",");
        auto errorCode = isTableCreated(context, tableName, keyName, valueFiled);

        // not exist
        if (errorCode == 0u)
        {
            auto sysTable = dynamic_pointer_cast<DBPrecompiled>(context->getPrecompiled(openTable(context, storage::SYSTEM_TABLE)));
            auto tableEntry = sysTable->getDB()->newEntry();
            tableEntry->setField("table_name", tableName);
            tableEntry->setField("key_field", keyName);
            tableEntry->setField("value_field", valueFiled);
            sysTable->getDB()->insert(tableName, tableEntry);
            LOG(DEBUG) << "DBFactory create table:" << tableName;
        }
        out = abi.abiIn("", errorCode);
        break;
    }
    default:
    {
        break;
    }
    }

    return out;
}

unsigned DBFactoryPrecompiled::isTableCreated(PrecompiledContext::Ptr context,
                                              const string &tableName, const string &keyField, const string &valueFiled)
{
    auto it = _name2Table.find(tableName);
    if (it != _name2Table.end())
        return TABLE_ALREADY_OPEN;
    auto sysTable = dynamic_pointer_cast<DBPrecompiled>(context->getPrecompiled(openTable(context, storage::SYSTEM_TABLE)));
    auto tableEntries = sysTable->getDB()->select(tableName, sysTable->getDB()->newCondition());
    if (tableEntries->size() == 0u)
        return TABLE_NOT_EXISTS;
    for (size_t i = 0; i < tableEntries->size(); ++i)
    {
        auto entry = tableEntries->get(i);
        if (keyField == entry->getField("key_field") &&
            valueFiled == entry->getField("value_field"))
        {
            LOG(DEBUG) << "DBFactory table already exists:" << tableName;
            return TABLENAME_ALREADY_EXISTS;
        }
    }
    LOG(DEBUG) << "DBFactory tableName conflict:" << tableName;
    return TABLENAME_CONFLICT;
}

Address DBFactoryPrecompiled::openTable(PrecompiledContext::Ptr context, const string &tableName)
{
    auto it = _name2Table.find(tableName);
    if (it != _name2Table.end())
    {
        LOG(DEBUG) << "Table:" << tableName << " already open:" << it->second;
        return it->second;
    }
 
    LOG(DEBUG) << "Open table:" << tableName;
    storage::DB::Ptr db = _memoryDBFactory->openTable(
            context->blockInfo().hash, context->blockInfo().number.convert_to<int>(), tableName);
    if(!db) 
    {
        LOG(DEBUG) << "Open new table:" << tableName << " failed.";
        return Address();
    }
    DBPrecompiled::Ptr dbPrecompiled = make_shared<DBPrecompiled>();
    dbPrecompiled->setDB(db);
    Address address = context->registerPrecompiled(dbPrecompiled);
    _name2Table.insert(make_pair(tableName, address));
    return address;
}

h256 DBFactoryPrecompiled::hash(shared_ptr<PrecompiledContext> context)
{
    bytes data;

    LOG(DEBUG) << "this: " << this << " Summary table:" << _name2Table.size();
    for (auto dbAddress : _name2Table)
    {
        DBPrecompiled::Ptr db =
            dynamic_pointer_cast<DBPrecompiled>(context->getPrecompiled(dbAddress.second));
        h256 hash = db->hash();
        LOG(DEBUG) << "Table:" << dbAddress.first << " hash:" << hash;
        if (hash == h256())
        {
            continue;
        }

        bytes dbHash = db->hash().asBytes();
        data.insert(data.end(), dbHash.begin(), dbHash.end());
    }

    if (data.empty())
    {
        return h256();
    }

    LOG(DEBUG) << "Compute DBFactoryPrecompiled data:" << data << " hash:" << dev::sha256(&data);

    _hash = dev::sha256(&data);
    return _hash;
}

