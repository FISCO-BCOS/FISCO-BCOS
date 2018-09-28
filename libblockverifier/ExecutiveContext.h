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
#include <libethcore/ChainOperationParams.h>
#include <libexecutivecontext/StateFace.h>
#include <libstorage/TableFactoryPrecompiled.h>
#include <memory>

namespace dev
{
namespace blockverifier
{
class ExecutiveContext : public std::enable_shared_from_this<ExecutiveContext>
{
public:
    typedef std::shared_ptr<ExecutiveContext> Ptr;

    ExecutiveContext(){};

    virtual ~ExecutiveContext(){};

    virtual bytes call(Address address, bytesConstRef param);

    virtual Address registerPrecompiled(Precompiled::Ptr p);

    virtual bool isPrecompiled(Address address);

    Precompiled::Ptr getPrecompiled(Address address);

    void setAddress2Precompiled(Address address, Precompiled::Ptr precompiled)
    {
        m_address2Precompiled.insert(std::make_pair(address, precompiled));
    }

    BlockInfo blockInfo() { return m_blockInfo; }
    void setBlockInfo(BlockInfo blockInfo) { m_blockInfo = blockInfo; }

    std::shared_ptr<StateFace> getState() { return m_stateFace; }
    void setState(std::shared_ptr<StateFace> state) { m_stateFace = state }

    void pushBackTransactionReceipt(TransactionReceipt receipt)
    {
        m_TransactionReceipts.push_back(receipt);
    }

    void commit()
    {
        commitState();
        commitPreCompiled();
    };

    void clear()
    {
        clearState();
        clearPreCompiled();
    };

    std::shared_ptr<storage::Table> getTable(const Address& address)
    {
        std::string tableName = "_contract_data_" + address.hex() + "_";
        TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
            std::dynamic_pointer_cast<TableFactoryPrecompiled>(getPrecompiled(Address(0x1001)));
        auto address = tableFactoryPrecompiled->openTable(shared_from_this(), tableName);
        if (address == Address())
            return nullptr;
        TablePrecompiled::Ptr tablePrecompiled =
            std::dynamic_pointer_cast<TablePrecompiled>(getPrecompiled(address));
        auto table = tablePrecompiled->getTable();
        return table;
    };

    virtual bool isOrginPrecompiled(Address const& _a) const
    {
        return precompiledContract.count(_a);
    }

    virtual bigint costOfOrginPrecompiled(Address const& _a, bytesConstRef _in) const
    {
        return precompiledContract.at(_a).cost(_in);
    }
    virtual void executeOrginPrecompiled(Address const& _a, bytesConstRef _in, bytesRef _out) const
    {
        return precompiledContract.at(_a).execute(_in, _out);
    }

    void setPrecompiledContract(
        std::unordered_map<Address, PrecompiledContract> const& precompiledContract)
    {
        m_precompiledContract = precompiledContract;
    }

private:
    void commitState() { m_stateFace.commit(); };

    void commitPreCompiled()
    {
        TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
            std::dynamic_pointer_cast<TableFactoryPrecompiled>(getPrecompiled(Address(0x1001)));
        tableFactoryPrecompiled->getmemoryTableFactory()->commit();
    };

    void clearState() { m_stateFace.clear(); };

    void clearPreCompiled()
    {
        TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
            std::dynamic_pointer_cast<TableFactoryPrecompiled>(getPrecompiled(Address(0x1001)));
        tableFactoryPrecompiled->getmemoryTableFactory()->clear();
    };

    std::unordered_map<Address, Precompiled::Ptr> m_address2Precompiled;
    int m_addressCount = 0x10000;
    BlockInfo m_blockInfo;
    std::shared_ptr<StateFace> m_stateFace;
    TransactionReceipts m_TransactionReceipts;
    std::unordered_map<Address, PrecompiledContract> m_precompiledContract;
};

}  // namespace blockverifier

}  // namespace dev
