/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file BlockVerifierInterface.h
 *  @author yujiechen
 *  @date 2018-10-09
 */
#pragma once
#include <libethcore/Block.h>
#include <memory>
namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
class PrecompiledContract;
class BlockVerifierInterface : public std::enable_shared_from_this<BlockVerifierInterface>
{
public:
    typedef std::shared_ptr<BlockVerifierInterface> Ptr;
    BlockVerifierInterface() = default;
    virtual ~BlockVerifierInterface(){};
    virtual std::shared_ptr<ExecutiveContext> executeBlock(dev::eth::Block& block, int stateType,
        std::unordered_map<Address, dev::eth::PrecompiledContract> const& precompiledContract) = 0;
};
}  // namespace blockverifier
}  // namespace dev
