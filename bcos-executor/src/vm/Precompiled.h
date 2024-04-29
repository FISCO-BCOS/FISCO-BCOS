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
#include "../precompiled/common/Utilities.h"
#include "bcos-codec/wrapper/CodecWrapper.h"
#include "bcos-executor/src/precompiled/common/PrecompiledGas.h"
#include "bcos-framework/storage/Table.h"
#include "bcos-table/src/StateStorage.h"
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/FixedBytes.h>
#include <functional>
#include <unordered_map>
#include <utility>

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
    using PrecompiledParams =
        std::function<void(const std::shared_ptr<executor::TransactionExecutive>& executive,
            PrecompiledExecResult::Ptr const& callParameters)>;

    Precompiled(crypto::Hash::Ptr _hashImpl) : m_hashImpl(std::move(_hashImpl))
    {
        assert(m_hashImpl);
        m_precompiledGasFactory = std::make_shared<PrecompiledGasFactory>();
        assert(m_precompiledGasFactory);
    }
    virtual ~Precompiled() = default;

    virtual std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) = 0;
    virtual bool isParallelPrecompiled() { return false; }

    virtual std::vector<std::string> getParallelTag(bytesConstRef, bool) { return {}; }

protected:
    std::map<std::string, uint32_t, std::less<>> name2Selector;
    [[no_unique_address]] std::unordered_map<uint32_t,
        std::pair<protocol::BlockVersion, PrecompiledParams>>
        selector2Func;
    crypto::Hash::Ptr m_hashImpl;

    void registerFunc(uint32_t _selector, PrecompiledParams _func,
        protocol::BlockVersion _minVersion = protocol::BlockVersion::V3_0_VERSION)
    {
        selector2Func.insert({_selector, {_minVersion, std::move(_func)}});
    }

    void registerFunc(std::string const& _funcName, PrecompiledParams _func,
        protocol::BlockVersion _minVersion = protocol::BlockVersion::V3_0_VERSION)
    {
        selector2Func.insert(
            {getFuncSelector(_funcName, m_hashImpl), {_minVersion, std::move(_func)}});
    }

    template <class F>
    void registerFuncF(uint32_t _selector, F _func,
        protocol::BlockVersion _minVersion = protocol::BlockVersion::V3_0_VERSION)
    {
        selector2Func.insert(
            {_selector, {_minVersion, [this](auto&& _executive, auto&& _callParameters) {
                             F(std::forward<decltype(_executive)>(_executive),
                                 std::forward<decltype(_callParameters)>(_callParameters));
                         }}});
    }

protected:
    std::shared_ptr<PrecompiledGasFactory> m_precompiledGasFactory;

private:
    template <typename F>
    void Invoker(F func, const std::shared_ptr<executor::TransactionExecutive>& executive,
        PrecompiledExecResult::Ptr const& callParameters)
    {
        _Invoker(func, executive, callParameters);
    }
    template <typename R, typename T, typename... Args>
    void _Invoker(R (T::*func)(Args...),
        const std::shared_ptr<executor::TransactionExecutive>& executive,
        PrecompiledExecResult::Ptr const& callParameters)
    {
        using ArgsType = std::tuple<typename std::decay_t<Args>...>;
        ArgsType tuple;
        Deserialize(callParameters->params(), tuple);
        CallFunc<R>(
            func, (T*)this, callParameters, tuple, std::make_index_sequence<sizeof...(Args)>{});
    }

    template <typename Tuple, std::size_t... I>
    void _Deserialize(
        bytesConstRef dataRef, CodecWrapper const& codec, Tuple& tup, std::index_sequence<I...>)
    {
        codec.decode(dataRef, std::get<I>(tup)...);
    }

    template <typename... Args>
    void Deserialize(bytesConstRef dataRef, CodecWrapper const& codec, std::tuple<Args...>& val)
    {
        _Deserialize(dataRef, val, std::make_index_sequence<sizeof...(Args)>{});
    }

    template <typename P>
    void SetCallResult(P& val, PrecompiledExecResult::Ptr res, CodecWrapper const& codec)
    {
        res->setExecResult(codec.encode(val));
    }

    template <typename Tuple, std::size_t... I>
    void _SetCallResult(PrecompiledExecResult::Ptr res, CodecWrapper const& codec, Tuple& tup,
        std::index_sequence<I...>)
    {
        res->setExecResult(codec.encode(std::get<I>(tup)...));
    }

    template <typename... Args>
    void SetCallResult(
        std::tuple<Args...>& val, PrecompiledExecResult::Ptr res, CodecWrapper const& codec)
    {
        _SetCallResult(res, val, std::make_index_sequence<sizeof...(Args)>{});
    }

    template <typename R, typename T, typename F, typename Tuple, std::size_t... I>
    void CallFunc(F func, T* pObj, const std::shared_ptr<executor::TransactionExecutive>& executive,
        PrecompiledExecResult::Ptr const& res, Tuple& tup, std::index_sequence<I...>)
    {
        auto const& blockContext = executive->blockContext();
        CodecWrapper codec(blockContext.hashHandler(), blockContext.isWasm());
        try
        {
            if constexpr (std::is_same_v<R, void>)
            {
                (pObj->*func)(std::get<I>(tup)...);
            }
            else if constexpr (!std::is_same_v<R, void>)
            {
                R ret = (pObj->*func)(std::get<I>(tup)...);
                SetCallResult(std::move(ret), res);
            }
        }
        catch (const std::exception& e)
        {
            res->setExecResult(codec.encode(std::string(e.what())));
        }
    }
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

namespace executor
{
struct PrecompiledAvailable
{
    precompiled::Precompiled::Ptr precompiled;
    std::function<bool(uint32_t, bool, ledger::Features const& features)> availableFunc;
};
class PrecompiledMap
{
public:
    using Ptr = std::shared_ptr<PrecompiledMap>;
    PrecompiledMap() = default;
    PrecompiledMap(PrecompiledMap const&) = default;
    PrecompiledMap(PrecompiledMap&&) = default;
    PrecompiledMap& operator=(PrecompiledMap const&) = delete;
    PrecompiledMap& operator=(PrecompiledMap&&) = default;
    ~PrecompiledMap() = default;

    auto insert(std::string const& _key, precompiled::Precompiled::Ptr _precompiled,
        protocol::BlockVersion minVersion = protocol::BlockVersion::RC4_VERSION,
        bool needAuth = false)
    {
        auto func = [minVersion, needAuth](
                        uint32_t version, bool isAuth, ledger::Features const& features) -> bool {
            bool flag = true;
            if (needAuth)
            {
                flag = isAuth;
            }
            return version >= minVersion && flag;
        };
        return m_map.insert({_key, {std::move(_precompiled), std::move(func)}});
    }

    auto insert(std::string const& _key, precompiled::Precompiled::Ptr _precompiled,
        std::function<bool(uint32_t, bool, ledger::Features const& features)> func)
    {
        return m_map.insert({_key, {std::move(_precompiled), std::move(func)}});
    }
    precompiled::Precompiled::Ptr at(std::string const&, uint32_t version, bool isAuth,
        ledger::Features const& features) const noexcept;
    bool contains(std::string const& key, uint32_t version, bool isAuth,
        ledger::Features const& features) const noexcept;
    size_t size() const noexcept { return m_map.size(); }

private:
    std::unordered_map<std::string, PrecompiledAvailable> m_map;
};
}  // namespace executor
}  // namespace bcos
