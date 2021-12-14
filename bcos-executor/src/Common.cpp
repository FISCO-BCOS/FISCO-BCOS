/*
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
 * @brief vm common
 * @file Common.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "Common.h"
#include "libutilities/Common.h"

using namespace bcos::protocol;

namespace bcos
{
bool hasWasmPreamble(const bytesConstRef& _input)
{
    return _input.size() >= 8 && _input[0] == 0 && _input[1] == 'a' && _input[2] == 's' &&
           _input[3] == 'm';
}

bool hasWasmPreamble(const bytes& _input)
{
    return hasWasmPreamble(bytesConstRef(_input.data(), _input.size()));
}

namespace executor
{
TransactionStatus toTransactionStatus(Exception const& _e)
{
    if (!!dynamic_cast<OutOfGasLimit const*>(&_e))
        return TransactionStatus::OutOfGasLimit;
    if (!!dynamic_cast<NotEnoughCash const*>(&_e))
        return TransactionStatus::NotEnoughCash;
    if (!!dynamic_cast<BadInstruction const*>(&_e))
        return TransactionStatus::BadInstruction;
    if (!!dynamic_cast<BadJumpDestination const*>(&_e))
        return TransactionStatus::BadJumpDestination;
    if (!!dynamic_cast<OutOfGas const*>(&_e))
        return TransactionStatus::OutOfGas;
    if (!!dynamic_cast<OutOfStack const*>(&_e))
        return TransactionStatus::OutOfStack;
    if (!!dynamic_cast<StackUnderflow const*>(&_e))
        return TransactionStatus::StackUnderflow;
    if (!!dynamic_cast<ContractAddressAlreadyUsed const*>(&_e))
        return TransactionStatus::ContractAddressAlreadyUsed;
    if (!!dynamic_cast<PrecompiledError const*>(&_e))
        return TransactionStatus::PrecompiledError;
    if (!!dynamic_cast<RevertInstruction const*>(&_e))
        return TransactionStatus::RevertInstruction;
    if (!!dynamic_cast<PermissionDenied const*>(&_e))
        return TransactionStatus::PermissionDenied;
    if (!!dynamic_cast<CallAddressError const*>(&_e))
        return TransactionStatus::CallAddressError;
    if (!!dynamic_cast<GasOverflow const*>(&_e))
        return TransactionStatus::GasOverflow;
    if (!!dynamic_cast<ContractFrozen const*>(&_e))
        return TransactionStatus::ContractFrozen;
    if (!!dynamic_cast<AccountFrozen const*>(&_e))
        return TransactionStatus::AccountFrozen;
    return TransactionStatus::Unknown;
}
}  // namespace executor

}  // namespace bcos
