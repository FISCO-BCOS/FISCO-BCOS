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
 * @brief block factory
 * @author: yujiechen
 * @date: 2019-11-12
 */

#pragma once
#include "Block.h"
#include "PartiallyBlock.h"

namespace bcos
{
namespace protocol
{
class BlockFactory
{
public:
    using Ptr = std::shared_ptr<BlockFactory>;
    BlockFactory() = default;
    virtual ~BlockFactory() {}
    virtual Block::Ptr createBlock() { return std::make_shared<Block>(); }
};

class PartiallyBlockFactory : public BlockFactory
{
public:
    PartiallyBlockFactory() = default;
    ~PartiallyBlockFactory() override {}
    Block::Ptr createBlock() override { return std::make_shared<PartiallyBlock>(); }
};

}  // namespace protocol
}  // namespace bcos
