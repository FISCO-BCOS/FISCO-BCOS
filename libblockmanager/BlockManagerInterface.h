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
 * @brief : external interface of block manager
 * @author: yujiechen
 * @date: 2018-09-21
 */
#pragma once
#include <libdevcore/Worker.h>
#include <libethcore/Common.h>
#include <libethcore/Transaction.h>
namespace dev
{
namespace blockmanager
{
class BlockManagerInterface
{
public:
    BlockManagerInterface() = default;
    virtual ~BlockManagerInterface(){};
    virtual uint64_t number() const = 0;
    virtual h256 numberHash(unsigned _i) const = 0;
    virtual std::vector<bytes> transactions(h256 const& _blockHash) = 0;
    virtual bool isNonceOk(dev::eth::Transaction const& _transaction, bool _needinsert = false) = 0;
};
}  // namespace blockmanager
}  // namespace dev
