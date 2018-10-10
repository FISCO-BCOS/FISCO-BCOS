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
/** @file ExecutiveContext.cpp
 *  @author mingzhenliu
 *  @date 20180921
 */


#include "ExecutiveContext.h"
#include <libdevcore/easylog.h>
#include <libethcore/Exceptions.h>
#include <libexecutivecontext/ExecutionResult.h>
#include <libstorage/TableFactoryPrecompiled.h>

using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev;

bytes ExecutiveContext::call(Address address, bytesConstRef param)
{
    try
    {
        LOG(TRACE) << "PrecompiledEngine call:" << m_blockInfo.hash << " " << m_blockInfo.number
                   << " " << address << " " << toHex(param);

        auto p = getPrecompiled(address);

        if (p)
        {
            bytes out = p->call(shared_from_this(), param);
            return out;
        }
        else
        {
            LOG(DEBUG) << "can't find address:" << address;
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Precompiled call error:" << e.what();

        throw dev::eth::PrecompiledError();
    }

    return bytes();
}

Address ExecutiveContext::registerPrecompiled(Precompiled::Ptr p)
{
    Address address(++m_addressCount);

    m_address2Precompiled.insert(std::make_pair(address, p));

    return address;
}


bool ExecutiveContext::isPrecompiled(Address address)
{
    LOG(TRACE) << "PrecompiledEngine isPrecompiled:" << m_blockInfo.hash << " " << address;

    auto p = getPrecompiled(address);

    if (p)
    {
        LOG(DEBUG) << "internal contract:" << address;
    }

    return p.get() != NULL;
}

Precompiled::Ptr ExecutiveContext::getPrecompiled(Address address)
{
    LOG(TRACE) << "PrecompiledEngine getPrecompiled:" << m_blockInfo.hash << " " << address;

    LOG(TRACE) << "address size:" << m_address2Precompiled.size();
    auto itPrecompiled = m_address2Precompiled.find(address);

    if (itPrecompiled != m_address2Precompiled.end())
    {
        return itPrecompiled->second;
    }

    return Precompiled::Ptr();
}

std::shared_ptr<storage::Table> ExecutiveContext::getTable(const Address& address)
{
    std::string tableName = "_contract_data_" + address.hex() + "_";
    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
        std::dynamic_pointer_cast<TableFactoryPrecompiled>(getPrecompiled(Address(0x1001)));
    auto addr = tableFactoryPrecompiled->openTable(shared_from_this(), tableName);
    if (addr == Address())
        return nullptr;
    TablePrecompiled::Ptr tablePrecompiled =
        std::dynamic_pointer_cast<TablePrecompiled>(getPrecompiled(addr));
    auto table = tablePrecompiled->getTable();
    return table;
}

std::shared_ptr<dev::eth::StateFace> ExecutiveContext::getState()
{
    return m_stateFace;
}
void ExecutiveContext::setState(std::shared_ptr<dev::eth::StateFace> state)
{
    m_stateFace = state;
}

bool ExecutiveContext::isOrginPrecompiled(Address const& _a) const
{
    return m_precompiledContract.count(_a);
}

std::pair<bool, bytes> ExecutiveContext::executeOrginPrecompiled(
    Address const& _a, bytesConstRef _in) const
{
    return m_precompiledContract.at(_a).execute(_in);
}

void ExecutiveContext::setPrecompiledContract(
    std::unordered_map<Address, PrecompiledContract> const& precompiledContract)
{
    m_precompiledContract = precompiledContract;
}