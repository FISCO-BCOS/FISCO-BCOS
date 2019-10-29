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
 * @brief:
 * @file: FakeBlockVerifier.cpp
 * @author: yujiechen
 * @date: 2018-10-10
 */
#pragma once
#include <libblockverifier/BlockVerifierInterface.h>
#include <memory>
using namespace dev::blockverifier;

namespace dev
{
namespace test
{
/// simple fake of blocksync
class FakeBlockverifier : public BlockVerifierInterface
{
public:
    std::shared_ptr<ExecutiveContext> executeBlock(dev::eth::Block&, BlockInfo const&) override
    {
        return std::make_shared<ExecutiveContext>();
    }
    dev::eth::TransactionReceipt::Ptr executeTransaction(
        const dev::eth::BlockHeader&, dev::eth::Transaction::Ptr) override
    {
        dev::eth::TransactionReceipt::Ptr receipt =
            std::make_shared<dev::eth::TransactionReceipt>();
        return receipt;
    }


private:
    std::shared_ptr<ExecutiveContext> m_execContext;
};
}  // namespace test
}  // namespace dev
