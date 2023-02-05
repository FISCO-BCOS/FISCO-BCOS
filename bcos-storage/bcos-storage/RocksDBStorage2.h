#pragma once
#include "bcos-concepts/Exception.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-utilities/Error.h>
#include <rocksdb/db.h>
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <type_traits>

namespace bcos::storage2::rocksdb
{

template <class ResolverType, class Item>
concept Resolver = requires(ResolverType&& resolver)
{
    // clang-format off
    { resolver.encode(std::declval<Item>()) } -> concepts::bytebuffer::ByteBuffer;
    { resolver.decode(std::string_view{})} -> std::convertible_to<Item>;
    // clang-format on
};

template <class AnyResolver, class Key>
struct ResolverEncodeReturnTrait
{
    using type = decltype(std::declval<AnyResolver>().encode(std::declval<Key>()));
};
template <class AnyResolver, class Key>
using ResolverEncodeReturnType = typename ResolverEncodeReturnTrait<AnyResolver, Key>::type;

struct RocksDBException : public bcos::Error
{
};

template <class KeyType, class ValueType, Resolver<KeyType> KeyResolver,
    Resolver<ValueType> ValueResolver>
class RocksDBStorage2
{
private:
    ::rocksdb::DB& m_rocksDB;
    [[no_unique_address]] KeyResolver m_keyResolver;
    [[no_unique_address]] ValueResolver m_valueResolver;

public:
    RocksDBStorage2(::rocksdb::DB& rocksDB) : m_rocksDB(rocksDB) {}
    RocksDBStorage2(
        ::rocksdb::DB& rocksDB, KeyResolver&& keyResolver, ValueResolver&& valueResolver)
      : RocksDBStorage2(rocksDB),
        m_keyResolver(std::forward<KeyResolver>(keyResolver)),
        m_valueResolver(std::forward<ValueResolver>(valueResolver))
    {}
    using Key = KeyType;
    using Value = ValueType;

    template <RANGES::input_range KeyRange>
    class ReadIterator
    {
        friend class RocksDBStorage;

    private:
        using StoreRangeType = std::conditional_t<std::is_rvalue_reference_v<KeyRange>,
            std::remove_reference_t<KeyRange>, KeyRange>;

        StoreRangeType m_keyRange;
        RANGES::iterator_t<KeyRange> m_keyRangeIt;

        boost::container::small_vector<::rocksdb::PinnableSlice, 1> m_results;
        boost::container::small_vector<::rocksdb::Status, 1> m_status;
        int64_t m_index = -1;
        [[no_unique_address]] ValueResolver m_valueResolver;

    public:
        ReadIterator(KeyRange&& keyRange) : m_keyRange(std::forward<KeyRange>(keyRange)) {}

        task::AwaitableValue<bool> next()
        {
            if (m_index == -1)
            {
                m_keyRangeIt = RANGES::begin(m_keyRange);
            }
            else
            {
                RANGES::advance(m_keyRangeIt);
            }
            return !(static_cast<size_t>(++m_index) == m_results.size());
        }
        task::AwaitableValue<bool> hasValue() const
        {
            auto exists = m_status[m_index].ok();
            task::AwaitableValue<bool> hasValueAwaitable(exists);
            return hasValueAwaitable;
        }
        task::AwaitableValue<RANGES::range_value_t<KeyRange> const&> key() const
        {
            return *m_keyRangeIt;
        }
        task::AwaitableValue<ValueType> value() const
        {
            task::AwaitableValue<ValueType> valueAwaitable(
                m_valueResolver.decode(m_results[m_index]));
            return valueAwaitable;
        }
    };

    auto read(RANGES::input_range auto&& keys) -> task::AwaitableValue<ReadIterator<decltype(keys)>>
    {
        task::AwaitableValue<ReadIterator<decltype(keys)>> readIteratorAwaitable(
            std::forward<decltype(keys)>(keys));
        auto& readIterator = readIteratorAwaitable.value();
        if constexpr (RANGES::sized_range<decltype(keys)>)
        {
            if (RANGES::size(keys) == 1)
            {
                readIterator.m_results.resize(1);
                readIterator.m_exists.resize(1);

                auto key = m_keyResolver.encode();
                auto status =
                    m_rocksDB.Get(::rocksdb::ReadOptions(), m_rocksDB.DefaultColumnFamily(),
                        ::rocksdb::Slice(RANGES::data(key), RANGES::size(key)),
                        &readIterator.m_results[0]);
                if (!status.ok() && !status.IsNotFound())
                {
                    BOOST_THROW_EXCEPTION(
                        RocksDBException{} << bcos::Error::ErrorMessage(status.ToString()));
                }

                return readIteratorAwaitable;
            }
        }

        auto encodedKeys =
            keys | RANGES::views::transform([this](const auto& key) {
                return m_keyResolver.encode(key);
            }) |
            RANGES::to<std::vector<
                ResolverEncodeReturnType<KeyResolver, RANGES::range_value_t<decltype(keys)>>>>();

        auto rocksDBKeys = encodedKeys | RANGES::views::transform([](const auto& encodedKey) {
            return ::rocksdb::Slice(RANGES::data(encodedKeys), RANGES::size(encodedKey));
        }) | RANGES::to<std::vector<::rocksdb::Slice>>();

        readIterator.m_results.resize(RANGES::size(rocksDBKeys));
        readIterator.m_status.resize(RANGES::size(rocksDBKeys));

        m_rocksDB.MultiGet(::rocksdb::ReadOptions(), m_rocksDB.DefaultColumnFamily(),
            rocksDBKeys.size(), rocksDBKeys.data(), readIterator.m_results.data(),
            readIterator.m_status.data());
        return readIteratorAwaitable;
    }

    task::AwaitableValue<void> write(
        RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
    {
        ::rocksdb::WriteBatch writeBatch;

        for (auto&& [key, value] : RANGES::zip_view(keys, values))
        {
            auto encodedKey = m_keyResolver.encode(key);
            auto encodedValue = m_valueResolver.encode(value);

            writeBatch.Put(Slice(RANGES::data(encodedKey), RANGES::size(encodedKey)),
                Slice(RANGES::data(encodedValue), RANGES::size(encodedValue)));
        }

        ::rocksdb::WriteOptions options;
        auto status = m_rocksDB.Write(options, &writeBatch);
        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }

        return {};
    }

    task::AwaitableValue<void> remove(RANGES::input_range auto const& keys)
    {
        ::rocksdb::WriteBatch writeBatch;

        for (auto const& key : keys)
        {
            auto encodedKey = m_keyResolver.encode(key);
            writeBatch.Delete(Slice(RANGES::data(encodedKey), RANGES::size(encodedKey)));
        }

        ::rocksdb::WriteOptions options;
        auto status = m_rocksDB.Write(options, &writeBatch);
        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }

        return {};
    }
};

}  // namespace bcos::storage2::rocksdb