#pragma once
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Task.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/Ranges.h>
#include <boost/throw_exception.hpp>
#include <new>
#include <type_traits>

namespace bcos::storage2::any_storage
{

struct UnsupportedMethodError : public bcos::Error
{
};

enum Attribute : int
{
    READABLE = 1,
    WRITEABLE = 2,
    SEEKABLE = 4,
    ERASABLE = 8
};

template <class KeyType, class ValueType>
class AnyReadIterator
{
private:
    class ReadIteratorBase
    {
    public:
        ReadIteratorBase() = default;
        virtual ~ReadIteratorBase() noexcept = default;
        ReadIteratorBase& operator=(const ReadIteratorBase&) = delete;
        ReadIteratorBase(const ReadIteratorBase&) = delete;
        ReadIteratorBase(ReadIteratorBase&&) noexcept = default;
        ReadIteratorBase& operator=(ReadIteratorBase&&) noexcept = default;

        virtual task::Task<bool> next() = 0;
        virtual task::Task<KeyType> key() = 0;
        virtual task::Task<ValueType> value() = 0;
        virtual task::Task<bool> hasValue() = 0;
    };
    template <class ReadIterator>
    class ReadIteratorImpl : public ReadIteratorBase
    {
    private:
        ReadIterator m_readIterator;

    public:
        ReadIteratorImpl(ReadIterator&& readIterator)
          : m_readIterator(std::forward<decltype(readIterator)>(readIterator))
        {}
        ~ReadIteratorImpl() noexcept override = default;
        ReadIteratorImpl& operator=(const ReadIteratorImpl&) = delete;
        ReadIteratorImpl(const ReadIteratorImpl&) = delete;
        ReadIteratorImpl(ReadIteratorImpl&&) noexcept = default;
        ReadIteratorImpl& operator=(ReadIteratorImpl&&) noexcept = default;

        task::Task<bool> next() override { co_return co_await m_readIterator.next(); }
        task::Task<KeyType> key() override { co_return co_await m_readIterator.key(); }
        task::Task<ValueType> value() override { co_return co_await m_readIterator.value(); }
        task::Task<bool> hasValue() override { co_return co_await m_readIterator.hasValue(); }
    };
    std::unique_ptr<ReadIteratorBase> m_readIterator;

public:
    using Key = KeyType;
    using Value = ValueType;

    template <class ReadIterator>
    AnyReadIterator(ReadIterator readIterator)
      : m_readIterator(std::make_unique<ReadIteratorImpl<ReadIterator>>(std::move(readIterator)))
    {}

    ~AnyReadIterator() noexcept = default;
    AnyReadIterator& operator=(const AnyReadIterator&) = default;
    AnyReadIterator(const AnyReadIterator&) = default;
    AnyReadIterator(AnyReadIterator&&) noexcept = default;
    AnyReadIterator& operator=(AnyReadIterator&&) noexcept = default;

    task::Task<bool> next() { co_return co_await m_readIterator->next(); }
    task::Task<KeyType> key() { co_return co_await m_readIterator->key(); }
    task::Task<ValueType> value() { co_return co_await m_readIterator->value(); }
    task::Task<bool> hasValue() { co_return co_await m_readIterator->hasValue(); }
};

template <class KeyType, class ValueType, Attribute attribute>
class AnyStorage
{
private:
    constexpr static bool withReadable = (attribute & Attribute::READABLE) != 0;
    constexpr static bool withWriteable = (attribute & Attribute::WRITEABLE) != 0;
    constexpr static bool withSeekable = (attribute & Attribute::SEEKABLE) != 0;
    constexpr static bool withErasable = (attribute & Attribute::ERASABLE) != 0;

    class StorageBase
    {
    public:
        virtual ~StorageBase() noexcept = default;
        StorageBase() = default;
        StorageBase& operator=(const StorageBase&) = default;
        StorageBase(const StorageBase&) = default;
        StorageBase(StorageBase&&) noexcept = default;
        StorageBase& operator=(StorageBase&&) noexcept = default;

        virtual task::Task<AnyReadIterator<KeyType, ValueType>> read(
            RANGES::any_view<KeyType> keys) = 0;
        virtual task::Task<void> write(
            RANGES::any_view<KeyType> keys, RANGES::any_view<ValueType> values) = 0;
        virtual task::Task<AnyReadIterator<KeyType, ValueType>> seek(const KeyType& key) = 0;
        virtual task::Task<AnyReadIterator<KeyType, ValueType>> seek(STORAGE_BEGIN_TYPE key) = 0;
        virtual task::Task<void> remove(RANGES::any_view<KeyType> keys) = 0;
    };
    template <class Storage, bool dummy = false>
    class StorageImpl : public StorageBase
    {
    private:
        Storage* m_storage = nullptr;

    public:
        StorageImpl(Storage* storage) : m_storage(storage) {}
        ~StorageImpl() noexcept override = default;
        StorageImpl& operator=(const StorageImpl&) = default;
        StorageImpl(const StorageImpl&) = default;
        StorageImpl(StorageImpl&&) noexcept = default;
        StorageImpl& operator=(StorageImpl&&) noexcept = default;

        task::Task<AnyReadIterator<KeyType, ValueType>> read(
            RANGES::any_view<KeyType> keys) override
        {
            if constexpr (withReadable && !dummy)
            {
                co_return AnyReadIterator<KeyType, ValueType>(
                    co_await m_storage->read(std::move(keys)));
            }
            else
            {
                BOOST_THROW_EXCEPTION(UnsupportedMethodError{});
            }
        }
        task::Task<void> write(
            RANGES::any_view<KeyType> keys, RANGES::any_view<ValueType> values) override
        {
            if constexpr (withWriteable && !dummy)
            {
                co_return co_await m_storage->write(std::move(keys), std::move(values));
            }
            else
            {
                BOOST_THROW_EXCEPTION(UnsupportedMethodError{});
            }
        }
        task::Task<AnyReadIterator<KeyType, ValueType>> seek(const KeyType& key) override
        {
            if constexpr (withSeekable && !dummy)
            {
                co_return co_await m_storage->seek(key);
            }
            else
            {
                BOOST_THROW_EXCEPTION(UnsupportedMethodError{});
            }
        }
        task::Task<AnyReadIterator<KeyType, ValueType>> seek(STORAGE_BEGIN_TYPE key) override
        {
            if constexpr (withSeekable && !dummy)
            {
                co_return co_await m_storage->seek(key);
            }
            else
            {
                BOOST_THROW_EXCEPTION(UnsupportedMethodError{});
            }
        }
        task::Task<void> remove(RANGES::any_view<KeyType> keys) override
        {
            if constexpr (withErasable && !dummy)
            {
                co_return co_await m_storage->remove(std::move(keys));
            }
            else
            {
                BOOST_THROW_EXCEPTION(UnsupportedMethodError{});
            }
        }
    };
    std::array<uint8_t, sizeof(StorageImpl<void, true>)> m_storage;

public:
    template <class Storage>
    AnyStorage(Storage& storage)
    {
        auto* base = (StorageBase*)m_storage.data();
        new (base) StorageImpl<Storage>(std::addressof(storage));
    }

    using Key = KeyType;
    using Value = ValueType;

    AnyStorage(AnyStorage&&) noexcept = default;
    AnyStorage& operator=(AnyStorage&&) noexcept = default;
    AnyStorage(const AnyStorage&) = delete;
    AnyStorage& operator=(const AnyStorage&) = delete;
    ~AnyStorage() noexcept
    {
        auto* base = (StorageBase*)m_storage.data();
        base->~StorageBase();
    }

    task::Task<AnyReadIterator<KeyType, ValueType>> read(RANGES::any_view<KeyType> keys)
        requires withReadable
    {
        auto* base = (StorageBase*)m_storage.data();
        co_return AnyReadIterator<KeyType, ValueType>(co_await base->read(std::move(keys)));
    }
    task::Task<void> write(RANGES::any_view<KeyType> keys, RANGES::any_view<ValueType> values)
        requires withWriteable
    {
        auto* base = (StorageBase*)m_storage.data();
        co_return co_await base->write(std::move(keys), std::move(values));
    }
    task::Task<AnyReadIterator<KeyType, ValueType>> seek(KeyType key)
        requires withSeekable
    {
        auto* base = (StorageBase*)m_storage.data();
        co_return co_await base->seek(std::move(key));
    }
    task::Task<AnyReadIterator<KeyType, ValueType>> seek(STORAGE_BEGIN_TYPE key)
    {
        auto* base = (StorageBase*)m_storage.data();
        co_return co_await base->seek(key);
    }
    task::Task<void> remove(RANGES::any_view<KeyType> keys)
        requires withErasable
    {
        StorageBase* base = m_storage.data();
        co_return co_await base->remove(keys);
    }
};

}  // namespace bcos::storage2::any_storage