#include "CRUDPrecompiled.h"
#include "libstorage/DBFactoryPrecompiled.h"
#include "libstorage/EntriesPrecompiled.h"
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::precompiled;

void CRUDPrecompiled::beforeBlock(PrecompiledContext::Ptr context) {}

void CRUDPrecompiled::afterBlock(PrecompiledContext::Ptr , bool )
{
}

std::string CRUDPrecompiled::toString(PrecompiledContext::Ptr)
{
    return "CRUD";
}

storage::DB::Ptr CRUDPrecompiled::openTable(
    PrecompiledContext::Ptr context, const std::string& tableName)
{
    LOG(DEBUG) << "CRUD open table:" << tableName;
    DBFactoryPrecompiled::Ptr dbFactoryPrecompiled = std::dynamic_pointer_cast<DBFactoryPrecompiled>(context->getPrecompiled(Address(0x1001)));
    auto address = dbFactoryPrecompiled->openTable(context, tableName);
    if(address == Address()) return nullptr;
    DBPrecompiled::Ptr dbPrecompiled = std::dynamic_pointer_cast<DBPrecompiled>(context->getPrecompiled(address));
    auto db = dbPrecompiled->getDB();
    return db;
}

bytes CRUDPrecompiled::call(PrecompiledContext::Ptr context, bytesConstRef param)
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
        storage::DB::Ptr db = openTable(context, tableName);
        if (db.get())
        {
            auto entries = db->select(key, db->newCondition());
            auto entriesPrecompiled = std::make_shared<EntriesPrecompiled>();
            entriesPrecompiled->setEntries(entries);
            auto newAddress = context->registerPrecompiled(entriesPrecompiled);
            out = abi.abiIn("", newAddress);
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

