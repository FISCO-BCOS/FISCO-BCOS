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
/** @file ExecutiveContext.h
 *  @author mingzhenliu
 *  @date 20180921
 */
#pragma once

#include "ExecutiveContext.h"
#include <libexecutive/StateFactoryInterface.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>

namespace dev
{
namespace precompiled
{
class PrecompiledExecResultFactory;
class PrecompiledGasFactory;
}  // namespace precompiled
namespace blockverifier
{
class ExecutiveContextFactory : public std::enable_shared_from_this<ExecutiveContextFactory>
{
public:
    typedef std::shared_ptr<ExecutiveContextFactory> Ptr;
    ExecutiveContextFactory(std::shared_ptr<dev::precompiled::PrecompiledExecResultFactory>
            _precompiledExecResultFactory);
    virtual ~ExecutiveContextFactory(){};

    virtual ExecutiveContext::Ptr createExecutiveContext(
        BlockInfo blockInfo, h256 const& stateRoot);

    virtual void setStateStorage(dev::storage::Storage::Ptr stateStorage);

    virtual void setStateFactory(
        std::shared_ptr<dev::executive::StateFactoryInterface> stateFactoryInterface);

    virtual void setTableFactoryFactory(dev::storage::TableFactoryFactory::Ptr tableFactoryFactory)
    {
        m_tableFactoryFactory = tableFactoryFactory;
    }

private:
    std::shared_ptr<dev::precompiled::PrecompiledExecResultFactory> m_precompiledExecResultFactory;
    std::shared_ptr<tbb::concurrent_unordered_map<Address,
        std::shared_ptr<precompiled::Precompiled>, std::hash<Address>>>
        m_buildInPrecompiledContract;

    dev::storage::TableFactoryFactory::Ptr m_tableFactoryFactory;
    dev::storage::Storage::Ptr m_stateStorage;
    std::shared_ptr<dev::executive::StateFactoryInterface> m_stateFactoryInterface;
    std::unordered_map<Address, dev::eth::PrecompiledContract> m_precompiledContract;

    void setTxGasLimitToContext(ExecutiveContext::Ptr context);
    void registerUserPrecompiled(std::shared_ptr<tbb::concurrent_unordered_map<Address,
            std::shared_ptr<precompiled::Precompiled>, std::hash<Address>>>
            _buildInPrecompiled);
};

}  // namespace blockverifier

}  // namespace dev
