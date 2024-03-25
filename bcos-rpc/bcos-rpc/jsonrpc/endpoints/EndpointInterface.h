/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file EndpointInterface.h
 * @author: kyonGuo
 * @date 2024/3/21
 */

#pragma once

#include "bcos-rpc/groupmgr/GroupManager.h"
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>

namespace bcos::rpc
{
class EndpointInterface
{
public:
    using UniquePtr = std::unique_ptr<EndpointInterface>;
    EndpointInterface() = default;
    virtual ~EndpointInterface() = default;
    EndpointInterface(const EndpointInterface&) = delete;
    EndpointInterface& operator=(const EndpointInterface&) = delete;
    EndpointInterface(EndpointInterface&&) = delete;
    EndpointInterface& operator=(EndpointInterface&&) = delete;

    virtual std::string getEntryName() const = 0;
    virtual MethodMap const& exportMethods() = 0;

protected:
    MethodMap m_methods;
};
}  // namespace bcos::rpc