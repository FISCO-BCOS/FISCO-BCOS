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
 * @file BalancePrecompiled.h
 * @author: wenlinli
 * @date 2023-10-26
 */

#include "BalancePrecompiled.h"
#include "AccountManagerPrecompiled.h"
#include "libinitializer/AuthInitializer.h"
#include <bcos-tool/BfsFileFactory.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/serialization/vector.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::ledger;

const char* const BALANCE_METHOD_GET_BALANCE = "getBalance(address)";
const char* const BALANCE_METHOD_SET_BALANCE = "setBalance(address,uint256)";
const char* const BALANCE_METHOD_ADD_BALANCE = "addBalance(address,uint256)";
const char* const BALANCE_METHOD_SUB_BALANCE = "subBalance(address,uint256)";
const char* const BALANCE_METHOD_TRANSFER = "transfer(address,address,uint256)";
const char* const BALANCE_METHOD_REGISTER_CALLER = "registerCaller(address)";
const char* const BALANCE_METHOD_UNREGISTER_CALLER = "unregisterCaller(address)";

BalancePrecompiled::BalancePrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[BALANCE_METHOD_GET_BALANCE] =
        getFuncSelector(BALANCE_METHOD_GET_BALANCE, _hashImpl);
    name2Selector[BALANCE_METHOD_SET_BALANCE] =
        getFuncSelector(BALANCE_METHOD_SET_BALANCE, _hashImpl);
    name2Selector[BALANCE_METHOD_ADD_BALANCE] =
        getFuncSelector(BALANCE_METHOD_ADD_BALANCE, _hashImpl);
    name2Selector[BALANCE_METHOD_SUB_BALANCE] =
        getFuncSelector(BALANCE_METHOD_SUB_BALANCE, _hashImpl);
    name2Selector[BALANCE_METHOD_TRANSFER] = getFuncSelector(BALANCE_METHOD_TRANSFER, _hashImpl);
    name2Selector[BALANCE_METHOD_REGISTER_CALLER] =
        getFuncSelector(BALANCE_METHOD_REGISTER_CALLER, _hashImpl);
    name2Selector[BALANCE_METHOD_UNREGISTER_CALLER] =
        getFuncSelector(BALANCE_METHOD_UNREGISTER_CALLER, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> BalancePrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    // [tableName][actualParams]
    std::vector<std::string> dynamicParams;
    bytes param;
    codec.decode(_callParameters->input(), dynamicParams, param);
    auto originParam = ref(param);
    uint32_t func = getParamFunc(originParam);

    /// directly passthrough data to call
    if (func == name2Selector[BALANCE_METHOD_GET_BALANCE])
    {
        getBalance(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_SET_BALANCE])
    {
        setBalance(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_ADD_BALANCE])
    {
        addBalance(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_SUB_BALANCE])
    {
        subBalance(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_TRANSFER])
    {
        transfer(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_REGISTER_CALLER])
    {
        registerCaller(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_UNREGISTER_CALLER])
    {
        unregisterCaller(_executive, _callParameters);
    }
    else
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("invalid function selector"));
    }
    return _callParameters;
}

void BalancePrecompiled::getBalance(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{}

void BalancePrecompiled::setBalance(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{}

void BalancePrecompiled::addBalance(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{}
void BalancePrecompiled::subBalance(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{}
void BalancePrecompiled::transfer(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{}
void BalancePrecompiled::registerCaller(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{}
void BalancePrecompiled::unregisterCaller(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{}
