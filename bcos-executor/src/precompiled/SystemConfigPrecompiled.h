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
 * @file SystemConfigPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-26
 */

#pragma once
#include "../vm/Precompiled.h"
#include "Common.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
namespace bcos
{
namespace precompiled
{
#if 0
contract SystemConfigTable
{
    // Return 1 means successful setting, and 0 means cannot find the config key.
    function setValueByKey(string key, string value) public returns(int256);
}
#endif

class SystemConfigPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<SystemConfigPrecompiled>;

    SystemConfigPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~SystemConfigPrecompiled(){};
    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender) override;
    std::string toString() override;
    std::pair<std::string, protocol::BlockNumber> getSysConfigByKey(
        const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& _key) const;

private:
    bool checkValueValid(std::string_view key, std::string_view value);
    std::map<std::string, std::function<bool(int64_t)>> m_sysValueCmp;
};

}  // namespace precompiled
}  // namespace bcos
