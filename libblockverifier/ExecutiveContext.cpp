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
#include <libprecompiled/ParallelConfigPrecompiled.h>
#include <libstorage/Table.h>

using namespace dev::executive;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev;
using namespace std;

bytes ExecutiveContext::call(Address const& origin, Address address, bytesConstRef param)
{
    try
    {
        /*
        EXECUTIVECONTEXT_LOG(TRACE) << LOG_DESC("[#call]PrecompiledEngine call")
                                    << LOG_KV("blockHash", m_blockInfo.hash.abridged())
                                    << LOG_KV("number", m_blockInfo.number)
                                    << LOG_KV("address", address) << LOG_KV("param", toHex(param));
                                    */

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
    return p.get() != NULL;
}

Precompiled::Ptr ExecutiveContext::getPrecompiled(Address address) const
{
    auto itPrecompiled = m_address2Precompiled.find(address);

    if (itPrecompiled != m_address2Precompiled.end())
    {
        return itPrecompiled->second;
    }
    return Precompiled::Ptr();
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

std::shared_ptr<std::vector<std::string>> ExecutiveContext::getTxCriticals(const Transaction& _tx)
{
    if (_tx.isCreation())
    {
        // Not to parallel contract creation transaction
        return nullptr;
    }

    auto p = getPrecompiled(_tx.receiveAddress());
    if (p)
    {
        // Precompile transaction
        if (p->isParallelPrecompiled())
        {
            auto ret = make_shared<vector<string>>(p->getParallelTag(ref(_tx.data())));
            for (string& critical : *ret)
            {
                critical += _tx.receiveAddress().hex();
            }
            return ret;
        }
        else
        {
            return nullptr;
        }
    }
    else
    {
        // Normal transaction
        auto parallelConfigPrecompiled =
            std::dynamic_pointer_cast<dev::precompiled::ParallelConfigPrecompiled>(
                getPrecompiled(Address(0x1006)));

        uint32_t selector = parallelConfigPrecompiled->getParamFunc(ref(_tx.data()));

        auto config = parallelConfigPrecompiled->getParallelConfig(
            shared_from_this(), _tx.receiveAddress(), selector, _tx.sender());

        if (config == nullptr)
        {
            return nullptr;
        }
        else
        {
            {  // Testing code
               // bytesConstRef data = parallelConfigPrecompiled->getParamData(ref(_tx.data()));

                AbiFunction af;
                af.setSignature(config->functionName);
                //
                bool isOk = af.doParser();
                if (!isOk)
                {
                    EXECUTIVECONTEXT_LOG(DEBUG)
                        << LOG_DESC("[#getTxCriticals] parser function signature failed, ")
                        << LOG_KV("func signature", config->functionName);

                    return nullptr;
                }

                auto res = make_shared<vector<string>>();
                ContractABI abi;
                isOk =
                    abi.abiOutByFuncSelector(ref(_tx.data()).cropped(4), af.getParamsTypes(), *res);
                if (!isOk)
                {
                    EXECUTIVECONTEXT_LOG(DEBUG) << LOG_DESC("[#getTxCriticals] abiout failed, ")
                                                << LOG_KV("func signature", config->functionName)
                                                << LOG_KV("input data", toHex(_tx.data()));

                    return nullptr;
                }

                res->resize((size_t)config->criticalSize);

                return res;
            }
        }
    }
}
