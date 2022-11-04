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
 * @brief host context for delegateCall
 * @file DelegateHostContext.h
 * @author: xingqiangbai
 * @date: 2022-09-30
 */
#pragma once
#include "HostContext.h"
#include <string>

namespace bcos
{
namespace executor
{
class DelegateHostContext : public HostContext
{
public:
    DelegateHostContext(CallParameters::UniquePtr callParameters,
        std::shared_ptr<TransactionExecutive> executive, std::string tableName);

    virtual ~DelegateHostContext() = default;
    std::optional<storage::Entry> code() override;
    bool setCode(bytes code) override;
    std::string_view caller() const override;

private:
    storage::Entry m_code;
    std::string m_delegateCallSender;
};

}  // namespace executor
}  // namespace bcos
