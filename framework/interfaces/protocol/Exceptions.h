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
 * @brief exceptions for protocol
 * @file Exceptions.h
 * @author: yujiechen
 * @date: 2021-03-16
 */
#pragma once
#include "../../libutilities/Exceptions.h"
namespace bcos
{
namespace protocol
{
DERIVE_BCOS_EXCEPTION(InvalidBlockHeader);
DERIVE_BCOS_EXCEPTION(InvalidSignatureList);
// transaction exceptions
DERIVE_BCOS_EXCEPTION(OutOfGasLimit);
DERIVE_BCOS_EXCEPTION(NotEnoughCash);
DERIVE_BCOS_EXCEPTION(BadInstruction);
DERIVE_BCOS_EXCEPTION(BadJumpDestination);
DERIVE_BCOS_EXCEPTION(OutOfGas);
DERIVE_BCOS_EXCEPTION(OutOfStack);
DERIVE_BCOS_EXCEPTION(StackUnderflow);
DERIVE_BCOS_EXCEPTION(ContractAddressAlreadyUsed);
DERIVE_BCOS_EXCEPTION(PrecompiledError);
DERIVE_BCOS_EXCEPTION(RevertInstruction);
DERIVE_BCOS_EXCEPTION(PermissionDenied);
DERIVE_BCOS_EXCEPTION(CallAddressError);
DERIVE_BCOS_EXCEPTION(GasOverflow);
DERIVE_BCOS_EXCEPTION(ContractFrozen);
DERIVE_BCOS_EXCEPTION(AccountFrozen);
}  // namespace protocol
}  // namespace bcos
