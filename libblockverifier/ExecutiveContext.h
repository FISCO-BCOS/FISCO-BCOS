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
/** @file ExecutiveContext.h
 *  @author mingzhenliu
 *  @date 20180921
 */
#pragma once

#include "Precompiled.h"
#include <libdevcore/FixedHash.h>
#include <libdevcrypto/Common.h>
#include <memory>

namespace dev
{
namespace blockverifier
{
class CallParams
{
public:
    typedef std::shared_ptr<CallParams> Ptr;
    Address senderAddress;
    Address codeAddress;
    Address receiveAddress;
    u256 valueTransfer;
    u256 apparentValue;
    u256 gas;
    bytesConstRef data;
    bytesRef out;
    //    OnOpFunc onOp;
};

class ExecutiveContext : public std::enable_shared_from_this<ExecutiveContext>
{
public:
    typedef std::shared_ptr<ExecutiveContext> Ptr;

    ExecutiveContext(){};

    virtual ~ExecutiveContext(){};

    // void Execute()

    virtual bytes call(CallParams::Ptr callParams)
    {
        bytes out;
        return out;
    };

    virtual Address registerPrecompiled(Precompiled::Ptr p)
    {
        Address addr;
        return addr;
    };

    virtual bool isPrecompiled(Address address) { return false; };

    Precompiled::Ptr getPrecompiled(Address address) { return m_address2Precompiled[address]; };

    void setAddress2Precompiled(Address address, Precompiled::Ptr precompiled)
    {
        m_address2Precompiled.insert(std::make_pair(address, precompiled));
    }

    BlockInfo blockInfo() { return m_blockInfo; }
    void setBlockInfo(BlockInfo blockInfo) { m_blockInfo = blockInfo; }

    // std::shared_ptr<State> getState() { return state; }
    // void setState(std::shared_ptr<State> state) { m_state = state }

    // std::shared_ptr<State> getState() { return state; }
    // void setState(std::shared_ptr<State> state) { m_state = state }
private:
    std::unordered_map<Address, Precompiled::Ptr> m_address2Precompiled;
    int m_addressCount = 0x10000;
    BlockInfo m_blockInfo;
    // std::shared_ptr<State> m_state;
};

}  // namespace blockverifier

}  // namespace dev
