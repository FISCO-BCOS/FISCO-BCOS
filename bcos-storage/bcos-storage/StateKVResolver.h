#pragma once
#include "bcos-concepts/ByteBuffer.h"
#include "bcos-concepts/Exception.h"
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
    static auto encode(storage::Entry&& entry) { return std::forward<decltype(entry)>(entry); }
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
    static std::string_view encode(const transaction_executor::StateKey& stateKey)
    {
        return {stateKey.data(), stateKey.size()};
    }
    static transaction_executor::StateKey encode(transaction_executor::StateKey&& stateKey)
    {
        return std::forward<transaction_executor::StateKey>(stateKey);
    }
    static transaction_executor::StateKey encode(
        const transaction_executor::StateKeyView& stateKeyView)
    {
        return transaction_executor::StateKey(stateKeyView);
    }
    static transaction_executor::StateKey decode(std::string_view view)
    {
        return transaction_executor::StateKey(std::string(view));
    }
    static transaction_executor::StateKey decode(std::string buffer)
    {
        return transaction_executor::StateKey(std::move(buffer));
    }
};

}  // namespace bcos::storage2::rocksdb