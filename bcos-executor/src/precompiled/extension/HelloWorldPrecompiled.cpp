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
 * @file HelloWorldPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-30
 */

#include "HelloWorldPrecompiled.h"
#include "../../executive/BlockContext.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;

/*
contract HelloWorld {
    function get() public constant returns(string);
    function set(string _m) public;
}
*/

// HelloWorldPrecompiled table name
const std::string HELLO_WORLD_TABLE_NAME = "hello_world";
// key field
const std::string HELLO_WORLD_KEY_FIELD = "key";
const std::string HELLO_WORLD_KEY_FIELD_NAME = "hello_key";
// value field
const std::string HELLO_WORLD_VALUE_FIELD = "value";

// get interface
const char* const HELLO_WORLD_METHOD_GET = "get()";
// set interface
const char* const HELLO_WORLD_METHOD_SET = "set(string)";

HelloWorldPrecompiled::HelloWorldPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[HELLO_WORLD_METHOD_GET] = getFuncSelector(HELLO_WORLD_METHOD_GET, _hashImpl);
    name2Selector[HELLO_WORLD_METHOD_SET] = getFuncSelector(HELLO_WORLD_METHOD_SET, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> HelloWorldPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHexString(_callParameters->input()));

    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());
    bytesConstRef data = _callParameters->params();
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_callParameters->input().size());

    auto table = _executive->storage().openTable(precompiled::getTableName(HELLO_WORLD_TABLE_NAME));
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (!table)
    {
        // table is not exist, create it.
        table = _executive->storage().createTable(
            precompiled::getTableName(HELLO_WORLD_TABLE_NAME), HELLO_WORLD_VALUE_FIELD);
        gasPricer->appendOperation(InterfaceOpcode::CreateTable);
        if (!table)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("set")
                                   << LOG_DESC("open table failed.");
            getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
            return callResult;
        }
    }
    if (func == name2Selector[HELLO_WORLD_METHOD_GET])
    {  // get() function call
        // default retMsg
        std::string retValue = "Hello World!";

        auto entry = table->getRow(HELLO_WORLD_KEY_FIELD_NAME);
        if (!entry)
        {
            gasPricer->updateMemUsed(entry->size());
            gasPricer->appendOperation(InterfaceOpcode::Select, 1);

            retValue = entry->getField(0);
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("get")
                                   << LOG_KV("value", retValue);
        }
        callResult->setExecResult(codec->encode(retValue));
    }
    else if (func == name2Selector[HELLO_WORLD_METHOD_SET])
    {  // set(string) function call
        std::string strValue;
        codec->decode(data, strValue);
        auto entry = table->getRow(HELLO_WORLD_KEY_FIELD_NAME);
        gasPricer->updateMemUsed(entry->size());
        gasPricer->appendOperation(InterfaceOpcode::Select, 1);
        entry->setField(0, strValue);

        table->setRow(HELLO_WORLD_KEY_FIELD_NAME, *entry);
        gasPricer->appendOperation(InterfaceOpcode::Update, 1);
        gasPricer->updateMemUsed(entry->size());
        getErrorCodeOut(callResult->mutableExecResult(), 1, *codec);
    }
    else
    {  // unknown function call
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled")
                               << LOG_DESC(" unknown function ") << LOG_KV("func", func);
        callResult->setExecResult(codec->encode(u256((int)CODE_UNKNOW_FUNCTION_CALL)));
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}
