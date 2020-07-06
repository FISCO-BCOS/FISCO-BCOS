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
/** @file ConsensusPrecompiled.cpp
 *  @author chaychen
 *  @date 20181211
 */

#include "ConsensusPrecompiled.h"
#include "libprecompiled/EntriesPrecompiled.h"
#include "libprecompiled/TableFactoryPrecompiled.h"
#include <libethcore/ABI.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

/*
contract ConsensusSystemTable
{
    function addSealer(string nodeID) public returns(int256);
    function addObserver(string nodeID) public returns(int256);
    function remove(string nodeID) public returns(int256);
}
*/

const char* const CSS_METHOD_ADD_SEALER = "addSealer(string)";
const char* const CSS_METHOD_ADD_SER = "addObserver(string)";
const char* const CSS_METHOD_REMOVE = "remove(string)";

ConsensusPrecompiled::ConsensusPrecompiled()
{
    name2Selector[CSS_METHOD_ADD_SEALER] = getFuncSelector(CSS_METHOD_ADD_SEALER);
    name2Selector[CSS_METHOD_ADD_SER] = getFuncSelector(CSS_METHOD_ADD_SER);
    name2Selector[CSS_METHOD_REMOVE] = getFuncSelector(CSS_METHOD_REMOVE);
}

PrecompiledExecResult::Ptr ConsensusPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin, Address const&)
{
    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    int count = 0;

    showConsensusTable(context);

    int result = 0;
    if (func == name2Selector[CSS_METHOD_ADD_SEALER])
    {
        // addSealer(string)
        std::string nodeID;
        abi.abiOut(data, nodeID);
        // Uniform lowercase nodeID
        boost::to_lower(nodeID);

        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addSealer func")
                               << LOG_KV("nodeID", nodeID);
        if (nodeID.size() != 128u)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                                   << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
            result = CODE_INVALID_NODEID;
        }
        else
        {
            storage::Table::Ptr table = openTable(context, SYS_CONSENSUS);

            auto condition = table->newCondition();
            condition->EQ(NODE_KEY_NODEID, nodeID);
            auto entries = table->select(PRI_KEY, condition);
            auto entry = table->newEntry();
            entry->setField(NODE_TYPE, NODE_TYPE_SEALER);
            entry->setField(PRI_COLUMN, PRI_KEY);
            entry->setField(NODE_KEY_ENABLENUM,
                boost::lexical_cast<std::string>(context->blockInfo().number + 1));

            if (entries.get())
            {
                if (entries->size() == 0u)
                {
                    entry->setField(NODE_KEY_NODEID, nodeID);
                    count = table->insert(PRI_KEY, entry, std::make_shared<AccessOptions>(origin));
                    if (count == storage::CODE_NO_AUTHORIZED)
                    {
                        PRECOMPILED_LOG(DEBUG)
                            << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("permission denied");
                        result = storage::CODE_NO_AUTHORIZED;
                    }
                    else
                    {
                        result = count;
                        PRECOMPILED_LOG(DEBUG)
                            << LOG_BADGE("ConsensusPrecompiled")
                            << LOG_DESC("addSealer successfully") << LOG_KV("result", result);
                    }
                }
                else
                {
                    count = table->update(
                        PRI_KEY, entry, condition, std::make_shared<AccessOptions>(origin));
                    if (count == storage::CODE_NO_AUTHORIZED)
                    {
                        PRECOMPILED_LOG(DEBUG)
                            << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("permission denied");
                        result = storage::CODE_NO_AUTHORIZED;
                    }
                    else
                    {
                        result = count;
                        PRECOMPILED_LOG(DEBUG)
                            << LOG_BADGE("ConsensusPrecompiled")
                            << LOG_DESC("addSealer successfully") << LOG_KV("result", result);
                    }
                }
            }
        }
    }
    else if (func == name2Selector[CSS_METHOD_ADD_SER])
    {
        // addObserver(string)
        std::string nodeID;
        abi.abiOut(data, nodeID);
        // Uniform lowercase nodeID
        boost::to_lower(nodeID);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addObserver func")
                               << LOG_KV("nodeID", nodeID);
        if (nodeID.size() != 128u)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                                   << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
            result = CODE_INVALID_NODEID;
        }
        else
        {
            storage::Table::Ptr table = openTable(context, SYS_CONSENSUS);

            auto condition = table->newCondition();
            condition->EQ(NODE_KEY_NODEID, nodeID);
            auto entries = table->select(PRI_KEY, condition);
            auto entry = table->newEntry();
            entry->setField(NODE_TYPE, NODE_TYPE_OBSERVER);
            entry->setField(PRI_COLUMN, PRI_KEY);
            entry->setField(NODE_KEY_ENABLENUM,
                boost::lexical_cast<std::string>(context->blockInfo().number + 1));

            if (entries->size() == 0u)
            {
                entry->setField(NODE_KEY_NODEID, nodeID);
                count = table->insert(PRI_KEY, entry, std::make_shared<AccessOptions>(origin));
                if (count == storage::CODE_NO_AUTHORIZED)
                {
                    PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("permission denied");
                    result = storage::CODE_NO_AUTHORIZED;
                }
                else
                {
                    result = count;
                    PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("ConsensusPrecompiled")
                        << LOG_DESC("addObserver successfully insert") << LOG_KV("result", result);
                }
            }
            else if (!checkIsLastSealer(table, nodeID))
            {
                count = table->update(
                    PRI_KEY, entry, condition, std::make_shared<AccessOptions>(origin));
                if (count == storage::CODE_NO_AUTHORIZED)
                {
                    result = storage::CODE_NO_AUTHORIZED;
                    PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("permission denied")
                        << LOG_KV("result", result);
                }
                else
                {
                    result = count;
                    PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("ConsensusPrecompiled")
                        << LOG_DESC("addObserver successfully update") << LOG_KV("result", result);
                }
            }
            else
            {
                result = CODE_LAST_SEALER;
            }
        }
    }
    else if (func == name2Selector[CSS_METHOD_REMOVE])
    {
        // remove(string)
        std::string nodeID;
        abi.abiOut(data, nodeID);
        // Uniform lowercase nodeID
        boost::to_lower(nodeID);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove func")
                               << LOG_KV("nodeID", nodeID);
        if (nodeID.size() != 128u)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                                   << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
            result = CODE_INVALID_NODEID;
        }
        else
        {
            storage::Table::Ptr table = openTable(context, SYS_CONSENSUS);

            if (!checkIsLastSealer(table, nodeID))
            {
                auto condition = table->newCondition();
                condition->EQ(NODE_KEY_NODEID, nodeID);
                count = table->remove(PRI_KEY, condition, std::make_shared<AccessOptions>(origin));
                if (count == storage::CODE_NO_AUTHORIZED)
                {
                    result = storage::CODE_NO_AUTHORIZED;
                    PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("permission denied")
                        << LOG_KV("result", result);
                }
                else
                {
                    result = count;
                    PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove successfully")
                        << LOG_KV("result", result);
                }
            }
            else
            {
                result = CODE_LAST_SEALER;
            }
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    getErrorCodeOut(callResult->mutableExecResult(), result);
    return callResult;
}

void ConsensusPrecompiled::showConsensusTable(ExecutiveContext::Ptr context)
{
    storage::Table::Ptr table = openTable(context, SYS_CONSENSUS);
    auto condition = table->newCondition();
    auto entries = table->select(PRI_KEY, condition);

    std::stringstream s;
    s << "ConsensusPrecompiled show table:\n";
    if (entries.get())
    {
        for (size_t i = 0; i < entries->size(); i++)
        {
            auto entry = entries->get(i);
            std::string name = entry->getField(PRI_COLUMN);
            std::string nodeID = entry->getField(NODE_KEY_NODEID);
            std::string type = entry->getField(NODE_TYPE);
            std::string enable = entry->getField(NODE_KEY_ENABLENUM);
            s << "ConsensusPrecompiled[" << i << "]:" << name << "," << nodeID << "," << type << ","
              << enable << "\n";
        }
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("showConsensusTable")
                           << LOG_KV("consensusTable", s.str());
}

bool ConsensusPrecompiled::checkIsLastSealer(storage::Table::Ptr table, std::string const& nodeID)
{
    // Check is last sealer or not.
    auto condition = table->newCondition();
    condition->EQ(NODE_TYPE, NODE_TYPE_SEALER);
    auto entries = table->select(PRI_KEY, condition);
    if (entries->size() == 1u && entries->get(0)->getField(NODE_KEY_NODEID) == nodeID)
    {
        // The nodeID in param is the last sealer, cannot be deleted.
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ConsensusPrecompiled")
                                 << LOG_DESC(
                                        "ConsensusPrecompiled the nodeID in param is the last "
                                        "sealer, cannot be deleted.")
                                 << LOG_KV("nodeID", nodeID);
        return true;
    }
    // check the last working sealer
    condition = table->newCondition();
    condition->EQ(NODE_KEY_NODEID, nodeID);
    entries = table->select(PRI_KEY, condition);
    if (entries->size() > 0 && entries->get(0)->getField(NODE_TYPE) == NODE_TYPE_WORKING_SEALER)
    {
        // check working sealer
        condition = table->newCondition();
        condition->EQ(NODE_TYPE, NODE_TYPE_WORKING_SEALER);
        entries = table->select(PRI_KEY, condition);
        if (entries->size() == 1u && entries->get(0)->getField(NODE_KEY_NODEID) == nodeID)
        {
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("ConsensusPrecompiled")
                << LOG_DESC("the nodeID in param is the last working sealer, cannot be deleted.")
                << LOG_KV("nodeID", nodeID);
            return true;
        }
    }
    return false;
}
