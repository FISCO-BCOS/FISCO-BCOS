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

#include "BlockChainInterface.h"
#include <libethcore/Block.h>
#include <libethcore/Common.h>
namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
}
namespace storage
{
class MemoryTableFactory;
}
namespace blockchain
{
class BlockChainImp : public BlockChainInterface
{
public:
    BlockChainImp(){};
    virtual ~BlockChainImp(){};
    int64_t number() const override;
    dev::h256 numberHash(int64_t _i) const override;
    std::shared_ptr<dev::eth::Block> getBlockByHash(dev::h256 const& _blockHash) override;
    std::shared_ptr<dev::eth::Block> getBlockByNumber(int64_t _i) override;
    void commitBlock(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext>) override;
    void setMemoryTableFactory(
        std::shared_ptr<dev::storage::MemoryTableFactory> memoryTableFactory);

private:
    void writeNumber(const dev::eth::Block& block);
    void writeTxToBlock(const dev::eth::Block& block);
    void writeBlockInfo(dev::eth::Block& block);
    void writeNumber2Hash(const dev::eth::Block& block);
    void writeHash2Block(dev::eth::Block& block);
    std::shared_ptr<dev::storage::MemoryTableFactory> m_memoryTableFactory;
    const std::string m_extraDbName_currentState = "currentState";
    const std::string m_keyValue_currentNumber = "currentNumber";
    const std::string m_ValueName_currentNumber = "value";
    const std::string m_ValueName = "value";
    const std::string m_extraDbName_txHash2Block = "txHash2Block";
    const std::string m_number2hash = "number2hash";
    const std::string m_hash2Block = "hash2Block";
};
}  // namespace blockchain
}  // namespace dev
