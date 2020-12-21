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
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @file: Common.h
 * @author: xingqiangbai
 * @date: 20200602
 */

#pragma once

#include "libprotocol/BlockHeader.h"
#include "libprotocol/LogEntry.h"
#include "libutilities/Common.h"
#include <evmc/instructions.h>
#include <functional>
#include <set>

namespace bcos
{
namespace blockverifier
{
class ExecutiveContext;
}
namespace executive
{
#define EXECUTIVE_LOG(LEVEL) LOG(LEVEL) << "[EXECUTIVE]"
struct SubState
{
    std::set<Address> suicides;  ///< Any accounts that have suicided.
    protocol::LogEntries logs;   ///< Any logs.
    u256 refunds;                ///< Refund counter of SSTORE nonzero->zero.

    SubState& operator+=(SubState const& _s)
    {
        suicides += _s.suicides;
        refunds += _s.refunds;
        logs += _s.logs;
        return *this;
    }

    void clear()
    {
        suicides.clear();
        logs.clear();
        refunds = 0;
    }
};

/**
 * @brief : execute the opcode of evm
 *
 */

/// set parameters and functions for the evm call
struct CallParameters
{
    CallParameters() = default;
    CallParameters(Address _senderAddress, Address _codeAddress, Address _receiveAddress,
        u256 _valueTransfer, u256 _apparentValue, u256 _gas, bytesConstRef _data)
      : senderAddress(_senderAddress),
        codeAddress(_codeAddress),
        receiveAddress(_receiveAddress),
        valueTransfer(_valueTransfer),
        apparentValue(_apparentValue),
        gas(_gas),
        data(_data)
    {}
    Address senderAddress;   /// address of the transaction sender
    Address codeAddress;     /// address of the contract
    Address receiveAddress;  /// address of the transaction receiver
    u256 valueTransfer;      /// transferred between the sender and receiver
    u256 apparentValue;
    u256 gas;
    bytesConstRef data;       /// transaction data
    bool staticCall = false;  /// only true when the transaction is a message call
};

/// the information related to the EVM
class EnvInfo
{
public:
    typedef std::function<bcos::h256(int64_t x)> CallBackFunction;

    // Constructor with custom gasLimit - used in some synthetic scenarios like eth_estimateGas RPC
    // method
    EnvInfo(protocol::BlockHeader const& _current, CallBackFunction _callback, u256 const& _gasUsed,
        u256 const& _gasLimit)
      : EnvInfo(_current, _callback, _gasUsed)
    {
        /// modify the gasLimit of current blockheader with given gasLimit
        m_headerInfo.setGasLimit(_gasLimit);
    }

    EnvInfo(protocol::BlockHeader const& _current, CallBackFunction _callback, u256 const& _gasUsed)
      : m_headerInfo(_current), m_numberHash(_callback), m_gasUsed(_gasUsed)
    {}

    EnvInfo() {}


    /// @return block header
    protocol::BlockHeader const& header() const { return m_headerInfo; }

    /// @return block number
    int64_t number() const { return m_headerInfo.number(); }

    /// @return timestamp
    uint64_t timestamp() const { return m_headerInfo.timestamp(); }

    /// @return gasLimit of the block header
    u256 const& gasLimit() const { return m_headerInfo.gasLimit(); }


    /// @return used gas of the evm
    u256 const& gasUsed() const { return m_gasUsed; }

    bcos::h256 numberHash(int64_t x) const { return m_numberHash(x); }

    std::shared_ptr<bcos::blockverifier::ExecutiveContext> precompiledEngine() const
    {
        return m_executiveEngine;
    }

    void setPrecompiledEngine(
        std::shared_ptr<bcos::blockverifier::ExecutiveContext> executiveEngine)
    {
        m_executiveEngine = executiveEngine;
    }


private:
    protocol::BlockHeader m_headerInfo;
    CallBackFunction m_numberHash;
    u256 m_gasUsed;
    std::shared_ptr<bcos::blockverifier::ExecutiveContext> m_executiveEngine;
};

}  // namespace executive
}  // namespace bcos
