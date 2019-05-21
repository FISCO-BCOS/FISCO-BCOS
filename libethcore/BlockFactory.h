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
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file BlockFactory.h
 *  @author yujiechen
 *  @date 2019/5/22
 */
#pragma once
#include "Block.h"
namespace dev
{
namespace eth
{
class BlockFactory
{
public:
    virtual ~BlockFactory() {}
    virtual dev::eth::Block::Ptr newBlock()
    {
        auto pBlock = std::make_shared<Block>();
        return pBlock;
    }
    virtual dev::eth::Block::Ptr newBlock(bytesConstRef _data,
        CheckTransaction const _option = CheckTransaction::Everything, bool _withReceipt = true,
        bool _withTxHash = false)
    {
        auto pBlock = std::make_shared<Block>(_data, _option, _withReceipt, _withTxHash);
        return pBlock;
    }

    virtual dev::eth::Block::Ptr newBlock(bytes const& _data,
        CheckTransaction const _option = CheckTransaction::Everything, bool _withReceipt = true,
        bool _withTxHash = false)
    {
        auto pBlock = std::make_shared<Block>(_data, _option, _withReceipt, _withTxHash);
        return pBlock;
    }

    virtual dev::eth::Block::Ptr newBlock(dev::eth::Block const& block)
    {
        auto pBlock = std::make_shared<Block>(block);
        return pBlock;
    }
};
}  // namespace eth
}  // namespace dev