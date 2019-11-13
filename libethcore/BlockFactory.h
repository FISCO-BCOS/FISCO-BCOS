/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */

/**
 * @brief block factory
 * @author: yujiechen
 * @date: 2019-11-12
 */

#pragma once
#include "Block.h"
#include "PartiallyBlock.h"

namespace dev
{
namespace eth
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

}  // namespace eth
}  // namespace dev