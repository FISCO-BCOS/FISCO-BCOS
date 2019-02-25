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
/** @file ExecutiveContext.cpp
 *  @author mingzhenliu
 *  @date 20180921
 */


#include "ExecutiveContext.h"
#include <libdevcore/easylog.h>
#include <libethcore/Exceptions.h>
#include <libexecutive/ExecutionResult.h>
#include <libstorage/TableFactoryPrecompiled.h>

using namespace dev::executive;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev;

bytes ExecutiveContext::call(Address const& origin, Address address, bytesConstRef param)
{
    try
    {
        EXECUTIVECONTEXT_LOG(TRACE)
            << LOG_DESC("[#call]PrecompiledEngine call") << LOG_KV("blockHash", m_blockInfo.hash)
            << LOG_KV("number", m_blockInfo.number) << LOG_KV("address", address)
            << LOG_KV("param", toHex(param));

        auto p = getPrecompiled(address);

        if (p)
        {
            bytes out = p->call(shared_from_this(), param, origin);
            return out;
        }
        else
        {
            EXECUTIVECONTEXT_LOG(DEBUG)
                << LOG_DESC("[#call]Can't find address") << LOG_KV("address", address);
        }
    }
    catch (std::exception& e)
    {
        EXECUTIVECONTEXT_LOG(ERROR) << LOG_DESC("[#call]Precompiled call error")
                                    << LOG_KV("EINFO", boost::diagnostic_information(e));

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


bool ExecutiveContext::isPrecompiled(Address address) const
{
    auto p = getPrecompiled(address);

    if (p)
    {
        LOG(DEBUG) << LOG_DESC("[#isPrecompiled]Internal contract") << LOG_KV("address", address);
    }

    return p.get() != NULL;
}

Precompiled::Ptr ExecutiveContext::getPrecompiled(Address address) const
{
    auto itPrecompiled = m_address2Precompiled.find(address);

    if (itPrecompiled != m_address2Precompiled.end())
    {
        return itPrecompiled->second;
    }
    /// since non-precompile contracts will print this log, modify the log level to DEBUG
    LOG(DEBUG) << LOG_DESC("[getPrecompiled] can't find precompiled") << LOG_KV("address", address);
    return Precompiled::Ptr();
}

std::shared_ptr<storage::Table> ExecutiveContext::getTable(const Address& address)
{
    std::string tableName = "_contract_data_" + address.hex() + "_";
    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
        std::dynamic_pointer_cast<TableFactoryPrecompiled>(getPrecompiled(Address(0x1001)));
    return tableFactoryPrecompiled->getmemoryTableFactory()->openTable(tableName);
}

std::shared_ptr<dev::executive::StateFace> ExecutiveContext::getState()
{
    return m_stateFace;
}
void ExecutiveContext::setState(std::shared_ptr<dev::executive::StateFace> state)
{
    m_stateFace = state;
}

bool ExecutiveContext::isOrginPrecompiled(Address const& _a) const
{
    return m_precompiledContract.count(_a);
}

std::pair<bool, bytes> ExecutiveContext::executeOriginPrecompiled(
    Address const& _a, bytesConstRef _in) const
{
    return m_precompiledContract.at(_a).execute(_in);
}

void ExecutiveContext::setPrecompiledContract(
    std::unordered_map<Address, PrecompiledContract> const& precompiledContract)
{
    m_precompiledContract = precompiledContract;
}

void ExecutiveContext::dbCommit(Block& block)
{
    m_stateFace->dbCommit(block.header().hash(), block.header().number());
    m_memoryTableFactory->commitDB(block.header().hash(), block.header().number());
}
