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

namespace bcos
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
    ExecutiveContextFactory()
    {
        m_precompiledContract.insert(std::make_pair(
            bcos::Address(1), bcos::eth::PrecompiledContract(3000, 0,
                                  bcos::eth::PrecompiledRegistrar::executor("ecrecover"))));
        m_precompiledContract.insert(std::make_pair(
            bcos::Address(2), bcos::eth::PrecompiledContract(
                                  60, 12, bcos::eth::PrecompiledRegistrar::executor("sha256"))));
        m_precompiledContract.insert(std::make_pair(
            bcos::Address(3), bcos::eth::PrecompiledContract(600, 120,
                                  bcos::eth::PrecompiledRegistrar::executor("ripemd160"))));
        m_precompiledContract.insert(std::make_pair(
            bcos::Address(4), bcos::eth::PrecompiledContract(
                                  15, 3, bcos::eth::PrecompiledRegistrar::executor("identity"))));
        m_precompiledContract.insert({bcos::Address{0x5},
            bcos::eth::PrecompiledContract(bcos::eth::PrecompiledRegistrar::pricer("modexp"),
                bcos::eth::PrecompiledRegistrar::executor("modexp"))});
        m_precompiledContract.insert({bcos::Address{0x6},
            bcos::eth::PrecompiledContract(
                150, 0, bcos::eth::PrecompiledRegistrar::executor("alt_bn128_G1_add"))});
        m_precompiledContract.insert({bcos::Address{0x7},
            bcos::eth::PrecompiledContract(
                6000, 0, bcos::eth::PrecompiledRegistrar::executor("alt_bn128_G1_mul"))});
        m_precompiledContract.insert({bcos::Address{0x8},
            bcos::eth::PrecompiledContract(
                bcos::eth::PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
                bcos::eth::PrecompiledRegistrar::executor("alt_bn128_pairing_product"))});
        m_precompiledContract.insert({bcos::Address{0x9},
            bcos::eth::PrecompiledContract(
                bcos::eth::PrecompiledRegistrar::pricer("blake2_compression"),
                bcos::eth::PrecompiledRegistrar::executor("blake2_compression"))});
    };
    virtual ~ExecutiveContextFactory(){};

    virtual void initExecutiveContext(
        BlockInfo blockInfo, h256 const& stateRoot, ExecutiveContext::Ptr context);

    virtual void setStateStorage(bcos::storage::Storage::Ptr stateStorage);

    virtual void setStateFactory(
        std::shared_ptr<bcos::executive::StateFactoryInterface> stateFactoryInterface);

    virtual void setTableFactoryFactory(bcos::storage::TableFactoryFactory::Ptr tableFactoryFactory)
    {
        m_tableFactoryFactory = tableFactoryFactory;
    }

    void setPrecompiledExecResultFactory(
        std::shared_ptr<bcos::precompiled::PrecompiledExecResultFactory>
            _precompiledExecResultFactory);

private:
    bcos::storage::TableFactoryFactory::Ptr m_tableFactoryFactory;
    bcos::storage::Storage::Ptr m_stateStorage;
    std::shared_ptr<bcos::executive::StateFactoryInterface> m_stateFactoryInterface;
    std::unordered_map<Address, bcos::eth::PrecompiledContract> m_precompiledContract;

    std::shared_ptr<bcos::precompiled::PrecompiledExecResultFactory> m_precompiledExecResultFactory;

    void setTxGasLimitToContext(ExecutiveContext::Ptr context);
    void registerUserPrecompiled(ExecutiveContext::Ptr context);
};

}  // namespace blockverifier

}  // namespace bcos
