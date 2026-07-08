#pragma once
#include "bcos-framework/transaction-executor/StateKey.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-utilities/Common.h>
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
    static void encode(const storage::Entry& entry, auto&& sink)
    {
        entry.encode(std::forward<decltype(sink)>(sink));
    }
    static storage::Entry decode(std::string_view view)
    {
        return storage::Entry::decode(bytesConstRef(
            reinterpret_cast<const bcos::byte*>(view.data()), view.size()));
    }
    static storage::Entry decode(std::string buffer)
    {
        return storage::Entry::decode(bytesConstRef(
            reinterpret_cast<const bcos::byte*>(buffer.data()), buffer.size()));
    }
};

struct StateKeyResolver
{
    static void encode(const executor_v1::StateKey& stateKey, auto&& sink)
    {
        sink(bytesConstRef(reinterpret_cast<const bcos::byte*>(stateKey.data()), stateKey.size()));
    }
    static void encode(const executor_v1::StateKeyView& stateKeyView, auto&& sink)
    {
        auto stateKey = executor_v1::StateKey(stateKeyView);
        sink(bytesConstRef(reinterpret_cast<const bcos::byte*>(stateKey.data()), stateKey.size()));
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