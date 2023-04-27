#pragma once
#include "bcos-concepts/Exception.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/AwaitableValue.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Error.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <type_traits>
#include <variant>

namespace bcos::storage2::rocksdb
{

template <class ResolverType, class Item>
concept Resolver = requires(ResolverType&& resolver) {
                       // clang-format off
    resolver.encode(std::declval<Item>());
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

// clang-format off
struct RocksDBException : public bcos::Error {};
struct UnsupportedMethod : public bcos::Error {};
struct UnexpectedItemType : public bcos::Error {};
// clang-format on

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
      : m_rocksDB(rocksDB),
        m_keyResolver(std::forward<KeyResolver>(keyResolver)),
        m_valueResolver(std::forward<ValueResolver>(valueResolver))
    {}
    using Key = KeyType;
    using Value = ValueType;

    class ReadIterator
    {
        friend class RocksDBStorage2;

    private:
        boost::container::small_vector<::rocksdb::PinnableSlice, 1> m_results;
        boost::container::small_vector<::rocksdb::Status, 1> m_status;
        int64_t m_index = -1;
        ValueResolver& m_valueResolver;

    public:
        using Key = KeyType const&;
        using Value = ValueType;

        ReadIterator(ValueResolver& valueResolver) : m_valueResolver(valueResolver) {}

        task::AwaitableValue<bool> next()
        {
            return {!(static_cast<size_t>(++m_index) == m_results.size())};
        }
        task::AwaitableValue<bool> hasValue() const { return {m_status[m_index].ok()}; }
        task::AwaitableValue<Key> key() const
        {
            static_assert(sizeof(*this) == 0U, "Unsupported method!");
        }
        task::AwaitableValue<Value> value() const
        {
            return {m_valueResolver.decode(m_results[m_index].ToStringView())};
        }
    };

    auto read(RANGES::input_range auto const& keys) & -> task::AwaitableValue<ReadIterator>
    {
        task::AwaitableValue<ReadIterator> readIteratorAwaitable(ReadIterator{m_valueResolver});
        auto& readIterator = readIteratorAwaitable.value();
        if constexpr (RANGES::sized_range<decltype(keys)>)
        {
            if (RANGES::size(keys) == 1)
            {
                readIterator.m_results.resize(1);
                readIterator.m_status.resize(1);

                auto key = m_keyResolver.encode(keys[0]);
                auto& status = readIterator.m_status[0];
                status = m_rocksDB.Get(::rocksdb::ReadOptions(), m_rocksDB.DefaultColumnFamily(),
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
            return ::rocksdb::Slice(RANGES::data(encodedKey), RANGES::size(encodedKey));
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

        for (auto&& [key, value] : RANGES::views::zip(keys, values))
        {
            auto encodedKey = m_keyResolver.encode(key);
            auto encodedValue = m_valueResolver.encode(value);

            writeBatch.Put(::rocksdb::Slice(RANGES::data(encodedKey), RANGES::size(encodedKey)),
                ::rocksdb::Slice(RANGES::data(encodedValue), RANGES::size(encodedValue)));
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
            writeBatch.Delete(::rocksdb::Slice(RANGES::data(encodedKey), RANGES::size(encodedKey)));
        }

        ::rocksdb::WriteOptions options;
        auto status = m_rocksDB.Write(options, &writeBatch);
        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }

        return {};
    }

    class SeekIterator
    {
        friend class RocksDBStorage2;

    private:
        std::unique_ptr<::rocksdb::Iterator> m_rocksDBIterator;
        RocksDBStorage2& m_self;
        bool m_started = false;

    public:
        using Key = KeyType;
        using Value = ValueType;

        SeekIterator(std::unique_ptr<::rocksdb::Iterator> rocksDBIterator, RocksDBStorage2& self)
          : m_rocksDBIterator(std::move(rocksDBIterator)), m_self(self)
        {}

        task::AwaitableValue<bool> next()
        {
            if (m_started)
            {
                m_rocksDBIterator->Next();
            }
            else
            {
                m_started = true;
            }
            return {m_rocksDBIterator->Valid()};
        }
        task::AwaitableValue<bool> hasValue() const { return {true}; }
        task::AwaitableValue<Key> key() const
        {
            auto slice = m_rocksDBIterator->key();
            return {m_self.m_keyResolver.decode(slice.ToStringView())};
        }
        task::AwaitableValue<Value> value() const
        {
            auto slice = m_rocksDBIterator->value();
            return {m_self.m_valueResolver.decode(slice.ToStringView())};
        }
    };

    task::AwaitableValue<SeekIterator> seek(auto const& key) &
    {
        task::AwaitableValue<SeekIterator> iteratorAwaitable(
            {std::unique_ptr<::rocksdb::Iterator>(
                 m_rocksDB.NewIterator(::rocksdb::ReadOptions(), m_rocksDB.DefaultColumnFamily())),
                *this});
        auto& iterator = iteratorAwaitable.value();
        if constexpr (std::is_same_v<storage2::STORAGE_BEGIN_TYPE,
                          std::remove_cvref_t<decltype(key)>>)
        {
            iterator.m_rocksDBIterator->SeekToFirst();
        }
        else
        {
            iterator.m_rocksDBIterator->Seek(
                ::rocksdb::Slice(RANGES::data(key), RANGES::size(key)));
        }
        return iteratorAwaitable;
    }

    auto refToPointer(auto&& value)
    {
        if constexpr (std::is_lvalue_reference_v<decltype(value)>)
        {
            return std::addressof(value);
        }
        else
        {
            return std::forward<decltype(value)>(value);
        }
    }

    auto& pointerToRef(auto& value)
    {
        using Type = std::remove_reference_t<decltype(value)>;
        if constexpr (std::is_pointer_v<Type>)
        {
            return *value;
        }
        else
        {
            return value;
        }
    }

    task::Task<void> merge(storage2::SeekableStorage auto& from)
    {
        ::rocksdb::WriteBatch writeBatch;

        auto it = co_await from.seek(storage2::STORAGE_BEGIN);
        using IteratorKeyType = task::AwaitableReturnType<decltype(it.key())>;
        using IteratorValueType = task::AwaitableReturnType<decltype(it.value())>;

        using BatchKeyType = std::conditional_t<std::is_lvalue_reference_v<IteratorKeyType>,
            std::add_pointer_t<std::remove_reference_t<IteratorKeyType>>, IteratorKeyType>;
        using BatchValueType = std::conditional_t<std::is_lvalue_reference_v<IteratorValueType>,
            std::add_pointer_t<std::remove_reference_t<IteratorValueType>>, IteratorValueType>;

        using KeyValue = std::tuple<BatchKeyType, BatchValueType>;
        using DeleteKey = BatchKeyType;
        using KeyValueBuffer =
            std::tuple<decltype(m_keyResolver.encode(std::declval<IteratorKeyType>())),
                decltype(m_valueResolver.encode(std::declval<IteratorValueType>()))>;
        using DeleteKeyBuffer = decltype(m_keyResolver.encode(std::declval<IteratorKeyType>()));
        using PipelineItem =
            std::variant<std::monostate, KeyValue, DeleteKey, KeyValueBuffer, DeleteKeyBuffer>;

        tbb::parallel_pipeline(std::thread::hardware_concurrency() * 10,
            tbb::make_filter<void, PipelineItem>(tbb::filter_mode::serial_in_order,
                [&](tbb::flow_control& control) {
                    PipelineItem output;
                    task::syncWait([&]() -> task::Task<void> {
                        if (!co_await it.next())
                        {
                            control.stop();
                            co_return;
                        }

                        if (co_await it.hasValue())
                        {
                            output.template emplace<KeyValue>(
                                refToPointer(co_await it.key()), refToPointer(co_await it.value()));
                        }
                        else
                        {
                            output.template emplace<DeleteKey>(refToPointer(co_await it.key()));
                        }
                    }());
                    return output;
                }) &
                tbb::make_filter<PipelineItem, PipelineItem>(tbb::filter_mode::parallel,
                    [&](PipelineItem&& input) {
                        PipelineItem output;
                        std::visit(
                            [&](auto& item) {
                                using ItemType = std::remove_cvref_t<decltype(item)>;
                                if constexpr (std::is_same_v<std::monostate, ItemType>)
                                {
                                    return;
                                }
                                else if constexpr (std::is_same_v<KeyValue, ItemType>)
                                {
                                    auto& [key, value] = item;
                                    output.template emplace<KeyValueBuffer>(
                                        m_keyResolver.encode(pointerToRef(key)),
                                        m_valueResolver.encode(pointerToRef(value)));
                                }
                                else if constexpr (std::is_same_v<DeleteKey, ItemType>)
                                {
                                    output.template emplace<DeleteKeyBuffer>(
                                        m_keyResolver.encode(pointerToRef(item)));
                                }
                                else
                                {
                                    BOOST_THROW_EXCEPTION(UnexpectedItemType{});
                                }
                            },
                            input);
                        return output;
                    }) &
                tbb::make_filter<PipelineItem, void>(
                    tbb::filter_mode::serial_out_of_order, [&](PipelineItem&& input) {
                        std::visit(
                            [&](auto& item) {
                                using ItemType = std::remove_cvref_t<decltype(item)>;
                                if constexpr (std::is_same_v<std::monostate, ItemType>)
                                {
                                    return;
                                }
                                else if constexpr (std::is_same_v<KeyValueBuffer, ItemType>)
                                {
                                    auto& [keyBuffer, valueBuffer] = item;
                                    writeBatch.Put(::rocksdb::Slice(RANGES::data(keyBuffer),
                                                       RANGES::size(keyBuffer)),
                                        ::rocksdb::Slice(
                                            RANGES::data(valueBuffer), RANGES::size(valueBuffer)));
                                }
                                else if constexpr (std::is_same_v<DeleteKeyBuffer, ItemType>)
                                {
                                    writeBatch.Delete(
                                        ::rocksdb::Slice(RANGES::data(item), RANGES::size(item)));
                                }
                                else
                                {
                                    BOOST_THROW_EXCEPTION(UnexpectedItemType{});
                                }
                            },
                            input);
                    }));
        ::rocksdb::WriteOptions options;
        auto status = m_rocksDB.Write(options, &writeBatch);
        if (!status.ok())
        {
            BOOST_THROW_EXCEPTION(RocksDBException{} << error::ErrorMessage(status.ToString()));
        }
        co_return;
    }
};

}  // namespace bcos::storage2::rocksdb