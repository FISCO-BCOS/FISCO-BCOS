#pragma once
#include "bcos-concepts/Exception.h"
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <fmt/format.h>
#include <boost/throw_exception.hpp>

namespace bcos::storage2::rocksdb
{

struct InvalidStateKey : public Error
{
};

struct StateKeyResolver
{
    /*
    Table: "/apps/"(5 byte) + evm address(20 byte), mostly 25byte
    Key: mostly 32 byte
    Split: ":" 1byte

    All: 25 + 1 + 32 = 58, expand to 64
    */
    constexpr static size_t SMALL = 64;
    using DBKey = boost::container::small_vector<char, SMALL>;
    constexpr static char TABLE_KEY_SPLIT = ':';

    DBKey encode(auto const& stateKey)
    {
        auto& [tableName, key] = stateKey;
        auto tableNameView = *tableName;

        DBKey buffer;
        buffer.reserve(tableNameView.size() + 1 + key.size());
        buffer.insert(buffer.end(), RANGES::begin(tableNameView), RANGES::begin(tableNameView));
        buffer.emplace_back(TABLE_KEY_SPLIT);
        buffer.insert(buffer.end(), RANGES::begin(key), RANGES::end(key));
        return buffer;
    }

    transaction_executor::StateKey decode(concepts::bytebuffer::ByteBuffer auto const& buffer)
    {
        auto it = RANGES::find(buffer, ':');
        if (it == RANGES::end(buffer))
        {
            BOOST_THROW_EXCEPTION(InvalidStateKey{} << error::ErrorMessage(
                                      fmt::format("Invalid state key! {}", buffer)));
        }

        auto tableRange = RANGES::subrange<decltype(buffer)>(RANGES::begin(buffer), it);
        RANGES::advance(it);
        auto keyRange = RANGES::subrange<decltype(buffer)>(it, RANGES::end(buffer));

        auto stateKey = transaction_executor::StateKey{tableRange, keyRange};
        return stateKey;
    }
};

struct ValueResolver
{
};

}  // namespace bcos::storage2::rocksdb