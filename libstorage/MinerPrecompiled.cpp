#include "MinerPrecompiled.h"
#include "libstorage/DBFactoryPrecompiled.h"
#include "libstorage/EntriesPrecompiled.h"
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::precompiled;

bytes MinerPrecompiled::call(PrecompiledContext::Ptr context, bytesConstRef param)
{
    LOG(TRACE) << "this: " << this << " call MinerPrecompiled:" << toHex(param);

    //解析出函数名
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;
    bytes out;
    switch (func)
    {
    case 0xb0c8f9dc:
    { // add(string)
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
            condition->EQ(NODE_KEY_NODEID, nodeID);
            auto entries = db->select(PRI_KEY, condition);
            auto entry = db->newEntry();
            entry->setField(NODE_TYPE, NODE_TYPE_MINER);
            if (entries->size() == 0u)
            {
                entry->setField(NODE_KEY_ENABLENUM, (context->blockInfo().number + 1).str());
                entry->setField(NODE_KEY_NODEID, nodeID);
                db->insert(PRI_KEY, entry);
                LOG(DEBUG) << "MinerPrecompiled new miner node, nodeID : " << nodeID;
            }
            else
            {
                db->update(PRI_KEY, entry, condition);
                LOG(DEBUG) << "MinerPrecompiled change to miner, nodeID : " << nodeID;
            }
            break;
        }
        LOG(ERROR) << "MinerPrecompiled open _sys_miners_ failed.";

        break;
    }
    case 0x80599e4b:
    { // remove(string)
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
            condition->EQ(NODE_KEY_NODEID, nodeID);
            auto entries = db->select(PRI_KEY, condition);
            auto entry = db->newEntry();
            entry->setField(NODE_TYPE, NODE_TYPE_OBSERVER);

            if (entries->size() == 0u)
            {
                LOG(DEBUG) << "MinerPrecompiled remove node not in _sys_miners_, nodeID : "
                           << nodeID;
                entry->setField(NODE_KEY_NODEID, nodeID);
                entry->setField(NODE_KEY_ENABLENUM, context->blockInfo().number.str());
                db->insert(PRI_KEY, entry);
            }
            else
            {
                db->update(PRI_KEY, entry, condition);
                LOG(DEBUG) << "MinerPrecompiled remove miner nodeID : " << nodeID;
            }
        }
        LOG(ERROR) << "MinerPrecompiled open _sys_miners_ failed.";
        break;
    }
    default:
    {
        break;
    }
    }

    return out;
}
