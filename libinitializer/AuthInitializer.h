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
 * @file AuthInitializer.h
 * @author: kyonRay
 * @date 2021-11-24
 */

#pragma once
#include "bcos-tool/NodeConfig.h"
#include "libinitializer/ProtocolInitializer.h"

namespace bcos::initializer
{
class AuthInitializer
{
public:
    static void init(protocol::BlockNumber _number,
        const std::shared_ptr<ProtocolInitializer>& _protocol,
        const std::shared_ptr<tool::NodeConfig>& _nodeConfig, const protocol::Block::Ptr& block);
};
}  // namespace bcos::initializer