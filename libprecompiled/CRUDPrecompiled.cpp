#include "CRUDPrecompiled.h"
#include "libstorage/DBFactoryPrecompiled.h"
#include "libstorage/DBPrecompiled.h"
#include "libstorage/EntriesPrecompiled.h"
#include "libstorage/MemoryDB.h"
#include <libdevcore/Hash.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace dev;
using namespace dev::precompiled;

void CRUDPrecompiled::beforeBlock(std::shared_ptr<PrecompiledContext> context) {}

void CRUDPrecompiled::afterBlock(std::shared_ptr<PrecompiledContext> context, bool commit)
{
    if (!commit)
    {
        LOG(DEBUG) << "Clear _name2Table: " << _name2Table.size();
        _name2Table.clear();

        return;
    }

    LOG(DEBUG) << "Submiting CRUDPrecompiled";

    //汇总所有表的数据并提交
    std::vector<dev::storage::TableData::Ptr> datas;

    for (auto &dbIt : _name2Table)
    {
        dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
        tableData->tableName = dbIt.first;
        for (auto &it : *(dbIt.second->data()))
        {
            if (it.second->dirty() || !_memoryDBFactory->stateStorage()->onlyDirty())
            {
                tableData->data.insert(std::make_pair(it.first, it.second));
            }
        }

        if (!tableData->data.empty())
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

std::string CRUDPrecompiled::toString(std::shared_ptr<PrecompiledContext>)
{
    return "CRUD";
}

storage::DB::Ptr CRUDPrecompiled::openTable(
    std::shared_ptr<PrecompiledContext> context, const std::string& tableName)
{
    LOG(DEBUG) << "CRUD open table:" << tableName;
    auto it = _name2Table.find(tableName);
    if (it != _name2Table.end())
    {
        LOG(DEBUG) << "Table:" << it->first  << " already opened";
        return it->second;
    }
    dev::storage::DB::Ptr db = _memoryDBFactory->openTable(
        context->blockInfo().hash, context->blockInfo().number.convert_to<int>(), tableName);

    _name2Table.insert(std::make_pair(tableName, db));
    return db;
}

bytes CRUDPrecompiled::call(std::shared_ptr<PrecompiledContext> context, bytesConstRef param)
{
    LOG(TRACE) << "this: " << this << " call CRUD:" << toHex(param);

    //解析出函数名
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;

    bytes out;

    switch (func)
    {
    case 0x73224cec:
    {  // select(string,string)
        std::string tableName, key;
        abi.abiOut(data, tableName, key);
        storage::MemoryDB::Ptr memoryDB =
            std::dynamic_pointer_cast<storage::MemoryDB>(openTable(context, tableName));
        auto entries = memoryDB->select(key, memoryDB->newCondition());
        auto entriesPrecompiled = std::make_shared<EntriesPrecompiled>();
        entriesPrecompiled->setEntries(entries);
        auto newAddress = context->registerPrecompiled(entriesPrecompiled);
        out = abi.abiIn("", newAddress);
        break;
    }
    default:
    {
        break;
    }
    }

    return out;
}

h256 CRUDPrecompiled::hash(std::shared_ptr<PrecompiledContext> context)
{
    bytes data;

    //汇总所有表的hash，算一个hash
    LOG(DEBUG) << "this: " << this << " 总计表:" << _name2Table.size();
    for (auto &it : _name2Table)
    {
        storage::DB::Ptr db = it.second;
        h256 hash = db->hash();
        LOG(DEBUG) << "表:" << it.first << " hash:" << hash;
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

    LOG(DEBUG) << "CRUDPrecompiled data:" << data << " hash:" << dev::sha256(&data);

    _hash = dev::sha256(&data);
    return _hash;
}
