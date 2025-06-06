#pragma once
#include "bcos-framework/transaction-executor/StateKey.h"
#include <bcos-framework/storage/Entry.h>
#include <fmt/format.h>
#include <boost/algorithm/hex.hpp>
#include <boost/throw_exception.hpp>

namespace bcos::storage2::rocksdb
{

struct InvalidStateKey : public Error
{
};

struct StateValueResolver
{
    static auto encode(const storage::Entry& entry) { return entry; }
    static auto encode(storage::Entry&& entry) { return std::move(entry); }
    static storage::Entry decode(std::string_view view)
    {
        storage::Entry entry;
        entry.set(view);
        return entry;
    }
    static storage::Entry decode(std::string buffer)
    {
        storage::Entry entry;
        entry.set(std::move(buffer));
        return entry;
    }
};

struct StateKeyResolver
{
    static std::string_view encode(const executor_v1::StateKey& stateKey)
    {
        return {stateKey.data(), stateKey.size()};
    }
    static executor_v1::StateKey encode(executor_v1::StateKey&& stateKey)
    {
        return std::move(stateKey);
    }
    static executor_v1::StateKey encode(const executor_v1::StateKeyView& stateKeyView)
    {
        return executor_v1::StateKey(stateKeyView);
    }
    static executor_v1::StateKey decode(std::string_view view)
    {
        return executor_v1::StateKey(std::string(view));
    }
    static executor_v1::StateKey decode(std::string buffer)
    {
        return executor_v1::StateKey(std::move(buffer));
    }
};

}  // namespace bcos::storage2::rocksdb