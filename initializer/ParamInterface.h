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
 * @brief : external interface for the param of ledger
 * @author: yujiechen
 * @date: 2018-10-23
 */
#pragma once
#include <memory>
#include <vector>
namespace dev
{
namespace initializer
{
/// forward class declaration
struct TxPoolParam;
struct ConsensusParam;
struct BlockChainParam;
struct P2pParam;
class LedgerParamInterface;
using LedgerParams = std::vector<LedgerParamInterface>;
/// interface for the systemParam
class SystemParamInterface
{
public:
    SystemParamInterface() = default;
    virtual ~SystemParamInterface() {}
    virtual LedgerParams const& getLedgerParams() const = 0;
    virtual LedgerParamInterface const& getLedgerParamByGroupId(uint16_t groupId) const = 0;
    virtual P2pParam const& getP2pParamByGroupId(uint16_t groupId) const = 0;
};

class LedgerParamInterface
{
public:
    LedgerParamInterface() = default;
    virtual ~LedgerParamInterface() {}
    virtual TxPoolParam const& txPoolParam() const = 0;
    virtual ConsensusParam const& consensusParam() const = 0;
    virtual BlockChainParam const& blockChainParam() const = 0;
    virtual int16_t const& groupId() const = 0;

protected:
    virtual void initLedgerParams() = 0;
};
}  // namespace initializer
}  // namespace dev
