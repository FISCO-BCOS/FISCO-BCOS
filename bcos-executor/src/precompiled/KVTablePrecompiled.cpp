/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file KVTablePrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-27
 */

#include "KVTablePrecompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/protocol/Exceptions.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const KV_TABLE_METHOD_SET = "set(string,string)";
const char* const KV_TABLE_METHOD_GET = "get(string)";

KVTablePrecompiled::KVTablePrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[KV_TABLE_METHOD_SET] = getFuncSelector(KV_TABLE_METHOD_SET, _hashImpl);
    name2Selector[KV_TABLE_METHOD_GET] = getFuncSelector(KV_TABLE_METHOD_GET, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> KVTablePrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    // [tableName][actualParams]
    std::vector<std::string> dynamicParams;
    bytes param;
    codec.decode(_callParameters->input(), dynamicParams, param);
    auto tableName = dynamicParams.at(0);
    tableName = getActualTableName(tableName);

    // get user call actual params
    auto originParam = ref(param);
    uint32_t func = getParamFunc(originParam);
    bytesConstRef data = getParamData(originParam);

    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_callParameters->input().size());

    auto table = _executive->storage().openTable(tableName);
    if (!table.has_value())
    {
        BOOST_THROW_EXCEPTION(PrecompiledError(tableName + " does not exist"));
    }

    if (func == name2Selector[KV_TABLE_METHOD_SET])
    {
        /// set(string,string)
        set(tableName, _executive, data, _callParameters, gasPricer);
    }
    else if (func == name2Selector[KV_TABLE_METHOD_GET])
    {
        /// get(string)
        get(tableName, _executive, data, _callParameters, gasPricer);
    }
    else
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("KVTablePrecompiled")
                              << LOG_DESC("call undefined function!");
        BOOST_THROW_EXCEPTION(PrecompiledError("KVTablePrecompiled call undefined function!"));
    }
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGas(_callParameters->m_gas - gasPricer->calTotalGas());
    return _callParameters;
}

void KVTablePrecompiled::get(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// get(string) => (bool, string)
    std::string key;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, key);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("KVTable") << LOG_KV("tableName", tableName)
                           << LOG_KV("get", key);
    auto table = _executive->storage().openTable(tableName);

    auto entry = table->getRow(key);
    gasPricer->appendOperation(InterfaceOpcode::Select);
    if (!entry)
    {
        callResult->setExecResult(codec.encode(false, std::string("")));
        return;
    }

    gasPricer->updateMemUsed(entry->size());
    callResult->setExecResult(codec.encode(true, std::string(entry->get())));
}

void KVTablePrecompiled::set(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// set(string,string)
    std::string key, value;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, key, value);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("KVTable") << LOG_KV("tableName", tableName)
                          << LOG_KV("key", key) << LOG_KV("value", value);

    checkLengthValidate(key, USER_TABLE_KEY_VALUE_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);

    checkLengthValidate(
        value, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);

    Entry entry;
    entry.importFields({value});
    _executive->storage().setRow(tableName, key, std::move(entry));
    callResult->setExecResult(codec.encode(int32_t(1)));
    gasPricer->setMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Insert);
}