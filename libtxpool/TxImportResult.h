/*
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @author wheatli
 * @date 2018.8.27
 * @modify add CallbackCollectionHandler.h
 *
 */

#pragma once

namespace bcos
{
namespace txpool
{
enum class ImportResult
{
    Success = 0,
    AlreadyInChain,
    AlreadyKnown,
    Malformed,
    TransactionNonceCheckFail,
    TxPoolNonceCheckFail,
    TransactionPoolIsFull,
    InvalidChainId,
    InvalidGroupId,
    BlockLimitCheckFailed,
    NotBelongToTheGroup,
    TransactionRefused,
    OverGroupMemoryLimit
};
}
}  // namespace bcos
