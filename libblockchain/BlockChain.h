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

/**
 * @brief : external interface of blockchain
 * @author: mingzhenliu
 * @date: 2018-09-21
 */
#pragma once
#include <memory>
#include <libdevcore/Worker.h>
#include <libblockverifier/BlockVerifier.h>
#include <libethcore/Common.h>
#include <libethcore/Block.h>
namespace dev
{
namespace blockchain
{
class BlockChainInterface
{
public:
    BlockChainInterface() = default;
    virtual ~BlockChainInterface(){};
    virtual int64_t number() const = 0;
    virtual h256 numberHash(unsigned _i) const = 0;
    virtual std::shared_ptr<dev::eth::Block> getBlockByHash(h256 const& _blockHash) = 0;
    virtual std::shared_ptr<dev::eth::Block> getBlockByNumber(unsigned _i) = 0;
    virtual void commitBlock(const dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext> ) = 0;
};
}  // namespace blockchain
}  // namespace dev
