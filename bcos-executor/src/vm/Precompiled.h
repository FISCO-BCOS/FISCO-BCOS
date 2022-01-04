/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief evm precompiled
 * @file Precompiled.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once
#include "../executive/TransactionExecutive.h"
#include "../precompiled/PrecompiledCodec.h"
#include "../precompiled/PrecompiledGas.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libstorage/StateStorage.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
#include <functional>
#include <unordered_map>

namespace bcos
{
namespace executor
{
class BlockContext;
using PrecompiledExecutor = std::function<std::pair<bool, bytes>(bytesConstRef _in)>;
using PrecompiledPricer = std::function<bigint(bytesConstRef _in)>;

DERIVE_BCOS_EXCEPTION(ExecutorNotFound);
DERIVE_BCOS_EXCEPTION(PricerNotFound);

class PrecompiledRegistrar
{
public:
    /// Get the executor object for @a _name function or @throw ExecutorNotFound if not found.
    static PrecompiledExecutor const& executor(std::string const& _name);

    /// Get the price calculator object for @a _name function or @throw PricerNotFound if not found.
    static PrecompiledPricer const& pricer(std::string const& _name);

    /// Register an executor. In general just use ETH_REGISTER_PRECOMPILED.
    static PrecompiledExecutor registerExecutor(
        std::string const& _name, PrecompiledExecutor const& _exec)
    {
        return (get()->m_execs[_name] = _exec);
    }
    /// Unregister an executor. Shouldn't generally be necessary.
    static void unregisterExecutor(std::string const& _name) { get()->m_execs.erase(_name); }

    /// Register a pricer. In general just use ETH_REGISTER_PRECOMPILED_PRICER.
    static PrecompiledPricer registerPricer(
        std::string const& _name, PrecompiledPricer const& _exec)
    {
        return (get()->m_pricers[_name] = _exec);
    }
    /// Unregister a pricer. Shouldn't generally be necessary.
    static void unregisterPricer(std::string const& _name) { get()->m_pricers.erase(_name); }

private:
    static PrecompiledRegistrar* get()
    {
        if (!s_this)
            s_this = new PrecompiledRegistrar;
        return s_this;
    }

    std::unordered_map<std::string, PrecompiledExecutor> m_execs;
    std::unordered_map<std::string, PrecompiledPricer> m_pricers;
    static PrecompiledRegistrar* s_this;
};

// TODO: unregister on unload with a static object.
#define ETH_REGISTER_PRECOMPILED(Name)                                                        \
    static std::pair<bool, bytes> __eth_registerPrecompiledFunction##Name(bytesConstRef _in); \
    static bcos::executor::PrecompiledExecutor __eth_registerPrecompiledFactory##Name =       \
        ::bcos::executor::PrecompiledRegistrar::registerExecutor(                             \
            #Name, &__eth_registerPrecompiledFunction##Name);                                 \
    static std::pair<bool, bytes> __eth_registerPrecompiledFunction##Name
#define ETH_REGISTER_PRECOMPILED_PRICER(Name)                                    \
    static bigint __eth_registerPricerFunction##Name(bytesConstRef _in);         \
    static bcos::executor::PrecompiledPricer __eth_registerPricerFactory##Name = \
        ::bcos::executor::PrecompiledRegistrar::registerPricer(                  \
            #Name, &__eth_registerPricerFunction##Name);                         \
    static bigint __eth_registerPricerFunction##Name

class PrecompiledContract
{
public:
    typedef std::shared_ptr<PrecompiledContract> Ptr;
    PrecompiledContract() = default;
    PrecompiledContract(PrecompiledPricer const& _cost, PrecompiledExecutor const& _exec,
        u256 const& _startingBlock = 0)
      : m_cost(_cost), m_execute(_exec), m_startingBlock(_startingBlock)
    {}

    PrecompiledContract(unsigned _base, unsigned _word, PrecompiledExecutor const& _exec,
        u256 const& _startingBlock = 0)
      : PrecompiledContract(
            [=](bytesConstRef _in) -> bigint {
                bigint s = _in.size();
                bigint b = _base;
                bigint w = _word;
                return b + (s + 31) / 32 * w;
            },
            _exec, _startingBlock)
    {}

    bigint cost(bytesConstRef _in) const { return m_cost(_in); }
    std::pair<bool, bytes> execute(bytesConstRef _in) const { return m_execute(_in); }

    u256 const& startingBlock() const { return m_startingBlock; }

private:
    PrecompiledPricer m_cost;
    PrecompiledExecutor m_execute;
    u256 m_startingBlock = 0;
};

}  // namespace executor
namespace precompiled
{
struct PrecompiledExecResult;
class PrecompiledGasFactory;
class Precompiled : public std::enable_shared_from_this<Precompiled>
{
public:
    using Ptr = std::shared_ptr<Precompiled>;

    Precompiled(crypto::Hash::Ptr _hashImpl) : m_hashImpl(_hashImpl)
    {
        assert(m_hashImpl);
        m_precompiledGasFactory = std::make_shared<PrecompiledGasFactory>();
        assert(m_precompiledGasFactory);
    }
    virtual ~Precompiled() = default;
    virtual std::string toString() { return ""; }
    virtual std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender) = 0;

    virtual bool isParallelPrecompiled() { return false; }
    virtual std::vector<std::string> getParallelTag(bytesConstRef, bool) { return {}; }

protected:
    std::map<std::string, uint32_t> name2Selector;
    crypto::Hash::Ptr m_hashImpl;

protected:
    std::optional<bcos::storage::Table> createTable(storage::StateStorage::Ptr _tableFactory,
        const std::string& _tableName, const std::string& _valueField);

    std::shared_ptr<PrecompiledGasFactory> m_precompiledGasFactory;
};

}  // namespace precompiled

namespace crypto
{
// sha2 - sha256 replace Hash.h begin
h256 sha256(bytesConstRef _input) noexcept;

h160 ripemd160(bytesConstRef _input);

/// Calculates the compression function F used in the BLAKE2 cryptographic hashing algorithm
/// Throws exception in case input data has incorrect size.
/// @param _rounds       the number of rounds
/// @param _stateVector  the state vector - 8 unsigned 64-bit little-endian words
/// @param _t0, _t1      offset counters - unsigned 64-bit little-endian words
/// @param _lastBlock    the final block indicator flag
/// @param _messageBlock the message block vector - 16 unsigned 64-bit little-endian words
/// @returns             updated state vector with unchanged encoding (little-endian)
bytes blake2FCompression(uint32_t _rounds, bytesConstRef _stateVector, bytesConstRef _t0,
    bytesConstRef _t1, bool _lastBlock, bytesConstRef _messageBlock);

std::pair<bool, bytes> ecRecover(bytesConstRef _in);
}  // namespace crypto
}  // namespace bcos
