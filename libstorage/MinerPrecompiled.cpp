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
            condition->EQ(MINER_KEY_NODEID, nodeID);
            auto entries = db->select(MINER_TYPE_MINER, condition);
            auto entry = db->newEntry();
            entry->setField(MINER_PRIME_KEY, MINER_TYPE_MINER);
            if (entries->size() == 0u)
            {
                entries = db->select(MINER_TYPE_OBSERVER, condition);
                if (entries->size() == 0u)
                {
                    entry->setField(MINER_KEY_NODEID, nodeID);
                    entry->setField(MINER_KEY_ENABLENUM, (context->blockInfo().number + 1).str());
                    db->insert(MINER_TYPE_MINER, entry);
                    LOG(DEBUG) << "MinerPrecompiled new miner node, nodeID : " << nodeID;
                    break;
                }
                LOG(DEBUG) << "MinerPrecompiled change to miner, nodeID : " << nodeID;
                db->update(MINER_TYPE_OBSERVER, entry, condition);
                break;
            }
            // in case levelDB select observer
            db->update(MINER_TYPE_MINER, entry, condition);
            LOG(DEBUG) << "MinerPrecompiled already miner, nodeID : " << nodeID;
            break;
        }
        LOG(ERROR) << "MinerPrecompiled open _sys_miners_ failed.";

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
            condition->EQ(MINER_KEY_NODEID, nodeID);
            auto entries = db->select(MINER_TYPE_MINER, condition);
            auto entry = db->newEntry();
            entry->setField(MINER_PRIME_KEY, MINER_TYPE_OBSERVER);
            entry->setField(MINER_KEY_NODEID, nodeID);
            entry->setField(MINER_KEY_ENABLENUM, context->blockInfo().number.str());
            if (entries->size() == 0u)
            {
                LOG(DEBUG) << "MinerPrecompiled remove node not in _sys_miners_, nodeID : "
                           << nodeID;
                db->insert(MINER_TYPE_OBSERVER, entry);
                break;
            }
            db->update(MINER_TYPE_MINER, entry, condition);
            LOG(DEBUG) << "MinerPrecompiled remove miner nodeID : " << nodeID;
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
