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
/** @file BlockVerifierInterface.h
 *  @author mingzhenliu
 *  @date 20180921
 */
#pragma once

#include "Common.h"
#include "ExecutiveContext.h"

#include <libdevcrypto/Common.h>
#include <libprotocol/Block.h>
#include <libprotocol/Transaction.h>
#include <libprotocol/TransactionReceipt.h>
#include <libutilities/FixedBytes.h>
#include <memory>

namespace bcos
{
namespace protocol
{
class PrecompiledContract;

}  // namespace protocol
namespace blockverifier
{
class BlockVerifierInterface
{
public:
    using Ptr = std::shared_ptr<BlockVerifierInterface>;
    BlockVerifierInterface() = default;

    virtual ~BlockVerifierInterface(){};

    virtual ExecutiveContext::Ptr executeBlock(
        bcos::protocol::Block& block, BlockInfo const& parentBlockInfo) = 0;

    virtual bcos::protocol::TransactionReceipt::Ptr executeTransaction(
        const bcos::protocol::BlockHeader& blockHeader, bcos::protocol::Transaction::Ptr _t) = 0;
};
}  // namespace blockverifier
}  // namespace bcos
