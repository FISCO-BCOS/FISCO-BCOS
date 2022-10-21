/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file BfsInitializer.h
 * @author: kyonGuo
 * @date 2022/10/20
 */

#pragma once
#include "Common.h"
#include <bcos-codec/wrapper/CodecWrapper.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-tool/NodeConfig.h>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::initializer;
namespace bcos::initializer
{
class BfsInitializer
{
public:
    BfsInitializer() = delete;
    static void init(protocol::BlockNumber _number,
        const std::shared_ptr<ProtocolInitializer>& _protocol,
        const std::shared_ptr<NodeConfig>& _nodeConfig, const protocol::Block::Ptr& _block)
    {
        bcos::CodecWrapper codecWrapper(
            _protocol->cryptoSuite()->hashImpl(), _nodeConfig->isWasm());
        bytes input = codecWrapper.encodeWithSig("initBfs()");

        auto transaction = _protocol->blockFactory()->transactionFactory()->createTransaction(3,
            _nodeConfig->isWasm() ? precompiled::BFS_NAME : precompiled::BFS_ADDRESS, input,
            u256(_number), 500, _nodeConfig->chainId(), _nodeConfig->groupId(), utcTime());
        _block->appendTransaction(std::move(transaction));
    }
};
}  // namespace bcos::initializer
