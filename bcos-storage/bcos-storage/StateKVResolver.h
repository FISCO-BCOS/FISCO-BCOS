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
    constexpr static char TABLE_KEY_SPLIT = ':';

    static auto encode(const transaction_executor::StateKey& stateKey)
    {
        return encodeToRocksDBKey(transaction_executor::StateKeyView(stateKey));
    }
    static auto encode(const transaction_executor::StateKeyView& stateKeyView)
    {
        return encodeToRocksDBKey(stateKeyView);
    }
    static transaction_executor::StateKey decode(
        concepts::bytebuffer::ByteBuffer auto const& buffer)
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

        try
        {
            if (tableRange.starts_with("/apps/") && tableRange.size() == 46 &&
                keyRange.size() == 32)
            {
                // Treat as an evm key
                evmc_address address;
                auto addressHexView = tableRange.substr(6);
                boost::algorithm::unhex(
                    addressHexView.begin(), addressHexView.end(), address.bytes);

                transaction_executor::StateKey stateKey(address, *(evmc_bytes32*)keyRange.data());
                return stateKey;
            }
        }
        catch (boost::algorithm::non_hex_input& e)
        {
            // No hex input? may be a normal key
        }

        transaction_executor::StateKey stateKey(tableRange, keyRange);
        return stateKey;
    }
};

}  // namespace bcos::storage2::rocksdb