#pragma once
#include "bcos-concepts/ByteBuffer.h"
#include "bcos-concepts/Exception.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <fmt/format.h>
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
    using DBKey = boost::container::small_vector<char,
        transaction_executor::ContractTable::static_capacity +
            transaction_executor::ContractKey::static_capacity>;
    constexpr static char TABLE_KEY_SPLIT = ':';

    static DBKey encode(auto&& stateKey)
    {
        auto& [tableName, key] = stateKey;

        DBKey buffer;
        buffer.insert(buffer.end(), RANGES::begin(tableName), RANGES::end(tableName));
        buffer.emplace_back(TABLE_KEY_SPLIT);
        buffer.insert(buffer.end(), RANGES::begin(key), RANGES::end(key));
        return buffer;
    }

    transaction_executor::StateKey decode(concepts::bytebuffer::ByteBuffer auto const& buffer)
    {
        std::string_view view = concepts::bytebuffer::toView(buffer);
        auto pos = view.find(TABLE_KEY_SPLIT);
        if (pos == std::string_view::npos)
        {
            BOOST_THROW_EXCEPTION(InvalidStateKey{} << error::ErrorMessage(
                                      fmt::format("Invalid state key! {}", buffer)));
        }

        auto tableRange = view.substr(0, pos);
        auto keyRange = view.substr(pos + 1, view.size());

        if (RANGES::empty(tableRange) || RANGES::empty(keyRange))
        {
            BOOST_THROW_EXCEPTION(InvalidStateKey{} << error::ErrorMessage(
                                      fmt::format("Empty table or key!", buffer)));
        }

        auto stateKey = std::make_tuple(transaction_executor::ContractTable(std::string_view(
                                            RANGES::data(tableRange), RANGES::size(tableRange))),
            transaction_executor::ContractKey(
                std::string_view(RANGES::data(keyRange), RANGES::size(keyRange))));
        return stateKey;
    }
};

}  // namespace bcos::storage2::rocksdb