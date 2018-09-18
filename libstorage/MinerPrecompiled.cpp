#include "MinerPrecompiled.h"
#include "libstorage/DBFactoryPrecompiled.h"
#include "libstorage/EntriesPrecompiled.h"
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::precompiled;

bytes MinerPrecompiled::call(PrecompiledContext::Ptr context, bytesConstRef param)
{
    LOG(TRACE) << "this: " << this << " call CRUD:" << toHex(param);

    //解析出函数名
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;
    bytes out;
    const std::string key("miner");
    switch (func)
    {
    case 0xb0c8f9dc:
    {  // add(string)
        std::string nodeID;
        abi.abiOut(data, nodeID);
        if (nodeID.size() != 128u)
        {
            LOG(DEBUG) << "NodeID length error. " << nodeID;
            break;
        }
        storage::DB::Ptr db = openTable(context, "_sys_miners_");
        if (db.get())
        {
            auto condition = db->newCondition();
            condition->EQ("node_id", nodeID);
            auto entries = db->select(key, condition);
            if (entries->size() == 0u)
            {
                auto entry = db->newEntry();
                entry->setField("type", key);
                entry->setField("node_id", nodeID);
                entry->setField("enable_num", context->blockInfo().number.str());
                db->insert(key, entry);
                LOG(DEBUG) << "MinerPrecompiled add miner nodeID : " << nodeID;
            }
        }
        break;
    }
    case 0x80599e4b:
    {  // remove(string)
        std::string nodeID;
        abi.abiOut(data, nodeID);
        if (nodeID.size() != 128u)
        {
            LOG(DEBUG) << "NodeID length error. " << nodeID;
            break;
        }
        storage::DB::Ptr db = openTable(context, "_sys_miners_");
        if (db.get())
        {
            auto condition = db->newCondition();
            condition->EQ("node_id", nodeID);
            auto entries = db->select(key, condition);
            if (entries->size() == 0u)
                break;
            db->remove(key, condition);
            LOG(DEBUG) << "MinerPrecompiled remove miner nodeID : " << nodeID;
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
