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
*/
/**
* @brief : Transaction DAG interface
* @author: jimmyshi
* @date: 2022-1-4
*/


#pragma once
#include "../CallParameters.h"
#include "../Common.h"
#include "../executive/BlockContext.h"
#include "../executive/TransactionExecutive.h"
#include "../executor/TransactionExecutor.h"
#include "CriticalFields.h"
#include <map>
#include <memory>
#include <queue>
#include <vector>

namespace bcos
{
namespace executor
{
class TransactionExecutive;
using ExecuteTxFunc = std::function<void(uint32_t)>;

enum ConflictFieldKind : std::uint8_t
{
    All = 0,
    Len,
    Env,
    Params,
    Const,
    None,
};

enum EnvKind : std::uint8_t
{
    Caller = 0,
    Origin,
    Now,
    BlockNumber,
    Addr,
};

class TxDAGInterface
{
public:
    virtual void init(critical::CriticalFieldsInterface::Ptr _txsCriticals, ExecuteTxFunc const& _f) = 0;

    virtual void run(unsigned int threadNum) = 0;
};
}}

