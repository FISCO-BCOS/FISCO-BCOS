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
/** @file KVTablePrecompiled.h
 *  @author xingqiangbai
 *  @date 20200206
 */
#include "KVTablePrecompiled.h"
#include "Common.h"
#include "ConditionPrecompiled.h"
#include "EntriesPrecompiled.h"
#include "EntryPrecompiled.h"
#include "libstorage/StorageException.h"
#include "libstorage/Table.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::storage;
using namespace dev::precompiled;
using namespace dev::blockverifier;

const char* const KVTABLE_METHOD_GET = "get(string)";
const char* const KVTABLE_METHOD_SET = "set(string,address)";
const char* const KVTABLE_METHOD_NEWENT = "newEntry()";


KVTablePrecompiled::KVTablePrecompiled()
{
    name2Selector[KVTABLE_METHOD_GET] = getFuncSelector(KVTABLE_METHOD_GET);
    name2Selector[KVTABLE_METHOD_SET] = getFuncSelector(KVTABLE_METHOD_SET);
    name2Selector[KVTABLE_METHOD_NEWENT] = getFuncSelector(KVTABLE_METHOD_NEWENT);
}

std::string KVTablePrecompiled::toString()
{
    return "KVTable";
}

PrecompiledExecResult::Ptr KVTablePrecompiled::call(ExecutiveContext::Ptr context,
    bytesConstRef param, Address const& origin, Address const& sender)
{
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTable") << LOG_DESC("call") << LOG_KV("func", func);
    dev::eth::ContractABI abi;

    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();

    callResult->gasPricer()->setMemUsed(param.size());

    if (func == name2Selector[KVTABLE_METHOD_GET])
    {  // get(string)
        std::string key;
        abi.abiOut(data, key);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTable") << LOG_KV("get", key);

        auto entries = m_table->select(key, m_table->newCondition());

        callResult->gasPricer()->updateMemUsed(getEntriesCapacity(entries));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Select, entries->size());

        if (entries->size() == 0)
        {
            callResult->setExecResult(abi.abiIn("", false, Address()));
        }
        else
        {
            auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
            // CachedStorage return entry use copy from
            entryPrecompiled->setEntry(
                std::const_pointer_cast<dev::storage::Entries>(entries)->get(0));
            auto newAddress = context->registerPrecompiled(entryPrecompiled);
            callResult->setExecResult(abi.abiIn("", true, newAddress));
        }
    }
    else if (func == name2Selector[KVTABLE_METHOD_SET])
    {  // set(string,address)
        if (!checkAuthority(context, origin, sender))
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("TablePrecompiled") << LOG_DESC("permission denied")
                << LOG_KV("origin", origin.hex()) << LOG_KV("contract", sender.hex());
            BOOST_THROW_EXCEPTION(PrecompiledException(
                "Permission denied. " + origin.hex() + " can't call contract " + sender.hex()));
        }
        std::string key;
        Address entryAddress;
        abi.abiOut(data, key, entryAddress);
        PRECOMPILED_LOG(INFO) << LOG_BADGE("KVTable") << LOG_KV("set", key);
        EntryPrecompiled::Ptr entryPrecompiled =
            std::dynamic_pointer_cast<EntryPrecompiled>(context->getPrecompiled(entryAddress));
        auto entry = entryPrecompiled->getEntry();
        checkLengthValidate(
            key, USER_TABLE_KEY_VALUE_MAX_LENGTH, CODE_TABLE_KEYVALUE_LENGTH_OVERFLOW, false);

        auto it = entry->begin();
        for (; it != entry->end(); ++it)
        {
            checkLengthValidate(it->second, USER_TABLE_FIELD_VALUE_MAX_LENGTH,
                CODE_TABLE_KEYVALUE_LENGTH_OVERFLOW, false);
        }
        auto entries = m_table->select(key, m_table->newCondition());

        callResult->gasPricer()->updateMemUsed(getEntriesCapacity(entries));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Select, entries->size());

        int count = 0;
        if (entries->size() == 0)
        {
            count = m_table->insert(key, entry, std::make_shared<AccessOptions>(origin));
            if (count > 0)
            {
                callResult->gasPricer()->setMemUsed(entry->capacity() * count);
                callResult->gasPricer()->appendOperation(InterfaceOpcode::Insert, count);
            }
        }
        else
        {
            count = m_table->update(
                key, entry, m_table->newCondition(), std::make_shared<AccessOptions>(origin));
            if (count > 0)
            {
                callResult->gasPricer()->setMemUsed(entry->capacity() * count);
                callResult->gasPricer()->appendOperation(InterfaceOpcode::Update, count);
            }
        }
        if (count == storage::CODE_NO_AUTHORIZED)
        {
            BOOST_THROW_EXCEPTION(
                PrecompiledException("Permission denied. " + origin.hex() + " can't write " +
                                     m_table->tableInfo()->name));
        }
        callResult->setExecResult(abi.abiIn("", s256(count)));
    }
    else if (func == name2Selector[KVTABLE_METHOD_NEWENT])
    {  // newEntry()
        auto entry = m_table->newEntry();
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
        entryPrecompiled->setEntry(entry);

        auto newAddress = context->registerPrecompiled(entryPrecompiled);
        callResult->setExecResult(abi.abiIn("", newAddress));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("KVTablePrecompiled")
                               << LOG_DESC("call undefined function!");
    }
    return callResult;
}

h256 KVTablePrecompiled::hash()
{
    return m_table->hash();
}
