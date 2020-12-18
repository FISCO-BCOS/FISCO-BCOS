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

#include <libprecompiled/ParallelConfigPrecompiled.h>
#include <libprotocol/ABIParser.h>
#include <libprotocol/Exceptions.h>
#include <libstorage/StorageException.h>
#include <libstorage/Table.h>

using namespace bcos::executive;
using namespace bcos::protocol;
using namespace bcos::protocol::abi;
using namespace bcos::blockverifier;
using namespace bcos;
using namespace std;
void ExecutiveContext::registerParallelPrecompiled(
    std::shared_ptr<bcos::precompiled::Precompiled> _precompiled)
{
    m_parallelConfigPrecompiled =
        std::dynamic_pointer_cast<bcos::precompiled::ParallelConfigPrecompiled>(_precompiled);
}

// set PrecompiledExecResultFactory for each precompiled object
void ExecutiveContext::setPrecompiledExecResultFactory(
    bcos::precompiled::PrecompiledExecResultFactory::Ptr _precompiledExecResultFactory)
{
    m_precompiledExecResultFactory = _precompiledExecResultFactory;
}

bcos::precompiled::PrecompiledExecResult::Ptr ExecutiveContext::call(
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
            return std::make_shared<bcos::precompiled::PrecompiledExecResult>();
        }
    }
    catch (bcos::precompiled::PrecompiledException& e)
    {
        EXECUTIVECONTEXT_LOG(ERROR)
            << "PrecompiledException" << LOG_KV("address", address) << LOG_KV("message:", e.what());
        BOOST_THROW_EXCEPTION(e);
    }
    catch (bcos::storage::StorageException const& e)
    {
        // throw PrecompiledError when supported_version < v2.7.0
        bcos::precompiled::PrecompiledException precompiledException(e);
        EXECUTIVECONTEXT_LOG(ERROR) << LOG_DESC("precompiledException") << LOG_KV("msg", e.what());
        throw precompiledException;
    }
    catch (std::exception& e)
    {
        EXECUTIVECONTEXT_LOG(ERROR) << LOG_DESC("[call]Precompiled call error")
                                    << LOG_KV("EINFO", boost::diagnostic_information(e));

        throw bcos::protocol::PrecompiledError();
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
    return (m_address2Precompiled.count(address));
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

std::shared_ptr<bcos::executive::StateFace> ExecutiveContext::getState()
{
    return m_stateFace;
}
void ExecutiveContext::setState(std::shared_ptr<bcos::executive::StateFace> state)
{
    m_stateFace = state;
}

bool ExecutiveContext::isEthereumPrecompiled(Address const& _a) const
{
    return m_precompiledContract.count(_a);
}

std::pair<bool, bytes> ExecutiveContext::executeOriginPrecompiled(
    Address const& _a, bytesConstRef _in) const
{
    return m_precompiledContract.at(_a).execute(_in);
}

bigint ExecutiveContext::costOfPrecompiled(Address const& _a, bytesConstRef _in) const
{
    return m_precompiledContract.at(_a).cost(_in);
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
        uint32_t selector = bcos::precompiled::getParamFunc(ref(_tx.data()));

        auto receiveAddress = _tx.receiveAddress();
        std::shared_ptr<bcos::precompiled::ParallelConfig> config = nullptr;
        // hit the cache, fetch ParallelConfig from the cache directly
        // Note: Only when initializing DAG, get ParallelConfig, will not get ParallelConfig during
        // transaction execution
        auto parallelKey = std::make_pair(receiveAddress, selector);
        if (m_parallelConfigCache.count(parallelKey))
        {
            config = m_parallelConfigCache[parallelKey];
        }
        // miss the cache, fetch ParallelConfig from the table and cache the config
        else
        {
            config = m_parallelConfigPrecompiled->getParallelConfig(
                shared_from_this(), receiveAddress, selector, _tx.sender());
            m_parallelConfigCache.insert(std::make_pair(parallelKey, config));
        }

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
                        << LOG_KV("input data", *toHexString(_tx.data()));

                    return nullptr;
                }

                paramTypes.resize((size_t)config->criticalSize);

                ContractABI abi;
                isOk =
                    abi.abiOutByFuncSelector(ref(_tx.data()).getCroppedData(4), paramTypes, *res);
                if (!isOk)
                {
                    EXECUTIVECONTEXT_LOG(DEBUG) << LOG_DESC("[getTxCriticals] abiout failed, ")
                                                << LOG_KV("func signature", config->functionName)
                                                << LOG_KV("input data", *toHexString(_tx.data()));

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
