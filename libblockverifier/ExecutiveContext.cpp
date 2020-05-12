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

#include <libethcore/ABIParser.h>
#include <libethcore/Exceptions.h>
#include <libexecutive/ExecutionResult.h>
#include <libprecompiled/ParallelConfigPrecompiled.h>
#include <libstorage/StorageException.h>
#include <libstorage/Table.h>

using namespace dev::executive;
using namespace dev::eth;
using namespace dev::eth::abi;
using namespace dev::blockverifier;
using namespace dev;
using namespace std;
// set PrecompiledExecResultFactory for each precompiled object
void ExecutiveContext::setPrecompiledExecResultFactory(
    dev::precompiled::PrecompiledExecResultFactory::Ptr _precompiledExecResultFactory)
{
    m_precompiledExecResultFactory = _precompiledExecResultFactory;
}

dev::precompiled::PrecompiledExecResult::Ptr ExecutiveContext::call(
    Address const& address, bytesConstRef param, Address const& origin, Address const& sender)
{
    try
    {
        auto p = getPrecompiled(address);

        if (p)
        {
            auto execResult = p->call(shared_from_this(), param, origin, sender);
            return execResult;
        }
        else
        {
            EXECUTIVECONTEXT_LOG(DEBUG)
                << LOG_DESC("[call]Can't find address") << LOG_KV("address", address);
            return std::make_shared<dev::precompiled::PrecompiledExecResult>();
        }
    }
    catch (dev::precompiled::PrecompiledException& e)
    {
        EXECUTIVECONTEXT_LOG(ERROR)
            << "PrecompiledException" << LOG_KV("address", address) << LOG_KV("message:", e.what());
        BOOST_THROW_EXCEPTION(e);
    }
    catch (dev::storage::StorageException& e)
    {
        EXECUTIVECONTEXT_LOG(ERROR) << "StorageException" << LOG_KV("address", address)
                                    << LOG_KV("errorCode", e.errorCode());
        throw dev::eth::PrecompiledError();
    }
    catch (std::exception& e)
    {
        EXECUTIVECONTEXT_LOG(ERROR) << LOG_DESC("[call]Precompiled call error")
                                    << LOG_KV("EINFO", boost::diagnostic_information(e));

        throw dev::eth::PrecompiledError();
    }
}

Address ExecutiveContext::registerPrecompiled(std::shared_ptr<precompiled::Precompiled> p)
{
    auto count = ++m_addressCount;
    Address address(count);
    if (!p->precompiledExecResultFactory())
    {
        p->setPrecompiledExecResultFactory(m_precompiledExecResultFactory);
    }
    m_address2Precompiled.insert(std::make_pair(address, p));

    return address;
}


bool ExecutiveContext::isPrecompiled(Address address) const
{
    auto p = getPrecompiled(address);
    return p.get() != NULL;
}

std::shared_ptr<precompiled::Precompiled> ExecutiveContext::getPrecompiled(Address address) const
{
    auto itPrecompiled = m_address2Precompiled.find(address);

    if (itPrecompiled != m_address2Precompiled.end())
    {
        return itPrecompiled->second;
    }
    return std::shared_ptr<precompiled::Precompiled>();
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
                auto res = make_shared<vector<string>>();

                ABIFunc af;
                bool isOk = af.parser(config->functionName);
                if (!isOk)
                {
                    EXECUTIVECONTEXT_LOG(DEBUG)
                        << LOG_DESC("[getTxCriticals] parser function signature failed, ")
                        << LOG_KV("func signature", config->functionName);

                    return nullptr;
                }

                auto paramTypes = af.getParamsType();
                if (paramTypes.size() < (size_t)config->criticalSize)
                {
                    EXECUTIVECONTEXT_LOG(DEBUG)
                        << LOG_DESC("[getTxCriticals] params type less than  criticalSize")
                        << LOG_KV("func signature", config->functionName)
                        << LOG_KV("func criticalSize", config->criticalSize)
                        << LOG_KV("input data", toHex(_tx.data()));

                    return nullptr;
                }

                paramTypes.resize((size_t)config->criticalSize);

                ContractABI abi;
                isOk = abi.abiOutByFuncSelector(ref(_tx.data()).cropped(4), paramTypes, *res);
                if (!isOk)
                {
                    EXECUTIVECONTEXT_LOG(DEBUG) << LOG_DESC("[getTxCriticals] abiout failed, ")
                                                << LOG_KV("func signature", config->functionName)
                                                << LOG_KV("input data", toHex(_tx.data()));

                    return nullptr;
                }

                for (string& critical : *res)
                {
                    critical += _tx.receiveAddress().hex();
                }

                return res;
            }
        }
    }
}
