#pragma once
#include "../ledger/LedgerTypeDef.h"
#include "../storage/Entry.h"
#include "../storage/Serialize.h"
#include "../storage2/Storage.h"
#include "../transaction-executor/StateKey.h"
#include "Features.h"
#include "bcos-task/Task.h"
#include <boost/lexical_cast.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>

namespace bcos::ledger
{
inline task::Task<void> Features::readFromStorage(
    storage2::ReadableStorage<executor_v1::StateKeyView> auto& storage, long blockNumber)
{
    for (auto key : bcos::ledger::Features::featureKeys())
    {
        auto entry =
            co_await storage2::readOne(storage, executor_v1::StateKeyView(ledger::SYS_CONFIG, key));
        if (entry)
        {
            auto [value, enableNumber] =
                storage::serialize::decode<ledger::SystemConfigEntry>(entry->get());
            if (blockNumber >= enableNumber)
            {
                set(key);
                setActivationBlock(string2Flag(key), enableNumber);
            }
        }
    }
}

inline task::Task<void> Features::writeToStorage(
    storage2::WritableStorage<executor_v1::StateKey, storage::Entry> auto& storage,
    long blockNumber, bool ignoreDuplicate) const
{
    for (auto [flag, name, value] : flags())
    {
        if (value && !(ignoreDuplicate && co_await storage2::existsOne(storage,
                                              executor_v1::StateKeyView(ledger::SYS_CONFIG, name))))
        {
            storage::Entry entry;
            entry.set(storage::serialize::encode(
                SystemConfigEntry{boost::lexical_cast<std::string>((int)value), blockNumber}));
            co_await storage2::writeOne(
                storage, executor_v1::StateKey(ledger::SYS_CONFIG, name), std::move(entry));
        }
    }
}

inline task::Task<void> readFromStorage(Features& features,
    storage2::ReadableStorage<executor_v1::StateKey> auto& storage, long blockNumber)
{
    decltype(auto) keys = bcos::ledger::Features::featureKeys();
    // Materialize the key views: a lazy range-v3 pipeline reports its size as
    // ranges::detail::diffmax_t, which storages like MemoryStorage cannot narrow to
    // size_t in readSome's reserve; a vector is a plain sized range for any storage.
    auto keyViews = keys | ::ranges::views::transform([](std::string_view key) {
        return executor_v1::StateKeyView(ledger::SYS_CONFIG, key);
    }) | ::ranges::to<std::vector>();
    auto entries = co_await storage2::readSome(std::forward<decltype(storage)>(storage), keyViews);
    for (auto&& [key, entry] : ::ranges::views::zip(keys, entries))
    {
        if (entry)
        {
            auto [value, enableNumber] =
                storage::serialize::decode<ledger::SystemConfigEntry>(entry->get());
            if (blockNumber >= enableNumber)
            {
                features.set(key);
                features.setActivationBlock(Features::string2Flag(key), enableNumber);
            }
        }
    }
}

inline task::Task<void> writeToStorage(Features const& features,
    storage2::WritableStorage<executor_v1::StateKey, executor_v1::StateValue> auto& storage,
    long blockNumber)
{
    decltype(auto) flags =
        features.flags() | ::ranges::views::filter([](auto&& tuple) { return std::get<2>(tuple); });
    co_await storage2::writeSome(std::forward<decltype(storage)>(storage),
        ::ranges::views::transform(flags, [&](auto&& tuple) {
            storage::Entry entry;
            entry.set(storage::serialize::encode(SystemConfigEntry{
                boost::lexical_cast<std::string>((int)std::get<2>(tuple)), blockNumber}));
            return std::make_tuple(
                executor_v1::StateKey(ledger::SYS_CONFIG, std::get<1>(tuple)), std::move(entry));
        }));
}

}  // namespace bcos::ledger
