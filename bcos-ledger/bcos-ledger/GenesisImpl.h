#pragma once

#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"
#include <evmc/evmc.h>
#include <range/v3/range.hpp>

namespace bcos::ledger
{

task::Task<void> setGenesisFeatures(
    ::ranges::input_range auto const& featureSets, Features& features)
    requires std::same_as<FeatureSet, std::decay_t<::ranges::range_value_t<decltype(featureSets)>>>
{
    for (auto&& featureSet : featureSets)
    {
        if (featureSet.enable > 0)
        {
            features.set(featureSet.flag);
        }
    }

    co_return;
}

task::Task<void> importGenesisState(::ranges::input_range auto const& allocs,
    const Features& features, auto& stateStorage, const crypto::Hash& hashImpl)
{
    for (auto&& importAccount : allocs)
    {
        evmc_address address;
        boost::algorithm::unhex(importAccount.address, address.bytes);

        account::EVMAccount account(
            stateStorage, address, features.get(Features::Flag::feature_raw_address));
        co_await account::create(account);

        if (!importAccount.code.empty())
        {
            bcos::bytes binaryCode;
            binaryCode.reserve(importAccount.code.size() / 2);
            boost::algorithm::unhex(importAccount.code, std::back_inserter(binaryCode));

            auto codeHash = hashImpl.hash(binaryCode);
            co_await account::setCode(account, std::move(binaryCode), std::string{}, codeHash);
        }

        if (!importAccount.nonce.empty())
        {
            co_await account::setNonce(account, std::move(importAccount.nonce));
        }

        if (importAccount.balance > 0)
        {
            co_await account::setBalance(account, importAccount.balance);
        }

        if (!importAccount.storage.empty())
        {
            for (auto const& [key, value] : importAccount.storage)
            {
                evmc_bytes32 evmKey;
                boost::algorithm::unhex(key, evmKey.bytes);
                evmc_bytes32 evmValue;
                boost::algorithm::unhex(value, evmValue.bytes);

                co_await account::setStorage(account, evmKey, evmValue);
            }
        }
    }
}

task::Task<std::vector<Alloc>> exportGenesisState(auto& stateStorage, const Features& features)
{
    // 只导出合约账户的状态数据，忽略其它数据
    // Only export the state data of contract accounts, ignoring other data.
    auto iterator = co_await storage2::range(stateStorage);
    while (co_await iterator.next())
    {
        transaction_executor::StateKey key = iterator.key();
        auto value = iterator.value();
    }
}

}  // namespace bcos::ledger