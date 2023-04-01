#pragma once
#include <bcos-task/Task.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/Ranges.h>
#include <boost/throw_exception.hpp>
#include <type_traits>

namespace bcos::storage2::any_storage
{

struct UnsupportedMethodError : public bcos::Error
{
};

enum Attribute : int
{
    READABLE = 0,
    WRITEABLE = 1,
    SEEKABLE = 2,
    ERASABLE = 4
};

template <class KeyType, class ValueType>
class AnyReadIterator
{
private:
    class ReadIteratorBase
    {
    public:
        virtual ~ReadIteratorBase() = default;
        ReadIteratorBase& operator=(const ReadIteratorBase&) = delete;
        ReadIteratorBase(const ReadIteratorBase&) = delete;
        ReadIteratorBase(ReadIteratorBase&&) noexcept = default;
        ReadIteratorBase& operator=(ReadIteratorBase&&) noexcept = default;

        virtual task::Task<void> next() = 0;
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
        ReadIteratorImpl(ReadIterator readIterator) : m_readIterator(std::move(readIterator)) {}

        task::Task<void> next() override { co_return co_await m_readIterator.next(); }
        task::Task<KeyType> key() override { co_return co_await m_readIterator.key(); }
        task::Task<ValueType> value() override { co_return co_await m_readIterator.value(); }
        task::Task<bool> hasValue() override { co_return co_await m_readIterator.hasValue(); }
    };
    std::unique_ptr<ReadIteratorBase> m_readIterator;

public:
    template <class ReadIterator>
    AnyReadIterator(ReadIterator readIterator)
      : m_readIterator(std::make_unique<ReadIteratorImpl<ReadIterator>>(std::move(readIterator)))
    {}

    virtual ~AnyReadIterator() = default;
    AnyReadIterator& operator=(const AnyReadIterator&) = delete;
    AnyReadIterator(const AnyReadIterator&) = delete;
    AnyReadIterator(AnyReadIterator&&) = delete;
    AnyReadIterator& operator=(AnyReadIterator&&) = delete;

    task::Task<void> next() { return m_readIterator->next(); }
    task::Task<KeyType> key() { return m_readIterator->key(); }
    task::Task<ValueType> value() { return m_readIterator->value(); }
    task::Task<bool> hasValue() { return m_readIterator->hasValue(); }
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
        virtual ~StorageBase() = default;
        StorageBase& operator=(const StorageBase&) = delete;
        StorageBase(const StorageBase&) = delete;
        StorageBase(StorageBase&&) noexcept = default;
        StorageBase& operator=(StorageBase&&) noexcept = default;

        virtual task::Task<AnyReadIterator<KeyType, ValueType>> read(
            RANGES::any_view<KeyType> keys) = 0;
        virtual task::Task<void> write(
            RANGES::any_view<KeyType> keys, RANGES::any_view<ValueType> values) = 0;
        virtual task::Task<AnyReadIterator<KeyType, ValueType>> seek(const KeyType& key) = 0;
        virtual task::Task<void> remove(RANGES::any_view<KeyType> keys) = 0;
    };
    template <class Storage>
    class StorageImpl : public StorageBase
    {
    private:
        Storage m_storage;

    public:
        StorageImpl(Storage storage) : m_storage(std::move(storage)) {}

        task::Task<AnyReadIterator<KeyType, ValueType>> read(
            RANGES::any_view<KeyType> keys) override
        {
            if constexpr (withReadable)
            {
                co_return AnyReadIterator<KeyType, ValueType>(
                    co_await m_storage.read(std::move(keys)));
            }
            else
            {
                BOOST_THROW_EXCEPTION(UnsupportedMethodError{});
            }
        }
        task::Task<void> write(
            RANGES::any_view<KeyType> keys, RANGES::any_view<ValueType> values) override
        {
            if constexpr (withWriteable)
            {
                co_return co_await m_storage.write(std::move(keys), std::move(values));
            }
            else
            {
                BOOST_THROW_EXCEPTION(UnsupportedMethodError{});
            }
        }
        task::Task<AnyReadIterator<KeyType, ValueType>> seek(const KeyType& key) override
        {
            if constexpr (withSeekable)
            {
                co_return co_await m_storage.seek(key);
            }
            else
            {
                BOOST_THROW_EXCEPTION(UnsupportedMethodError{});
            }
        }
        task::Task<void> remove(RANGES::any_view<KeyType> keys) override
        {
            if constexpr (withErasable)
            {
                co_return co_await m_storage.remove(std::move(keys));
            }
            else
            {
                BOOST_THROW_EXCEPTION(UnsupportedMethodError{});
            }
        }
    };
    std::unique_ptr<StorageBase> m_storage;

public:
    template <class Storage>
    AnyStorage(Storage storage)
      : m_storage(std::make_unique<StorageImpl<Storage>>(std::move(storage)))
    {}

    AnyStorage(AnyStorage&&) noexcept = default;
    AnyStorage& operator=(AnyStorage&&) noexcept = default;
    AnyStorage(const AnyStorage&) = delete;
    AnyStorage& operator=(const AnyStorage&) = delete;
    ~AnyStorage() = default;

    task::Task<AnyReadIterator<KeyType, ValueType>> read(RANGES::any_view<KeyType> keys)
        requires withReadable
    {
        return m_storage->read(std::move(keys));
    }
    task::Task<void> write(RANGES::any_view<KeyType> keys, RANGES::any_view<ValueType> values)
        requires withWriteable
    {
        return m_storage->write(std::move(keys), std::move(values));
    }
    task::Task<AnyReadIterator<KeyType, ValueType>> seek(KeyType key)
        requires withSeekable
    {
        return m_storage->seek(std::move(key));
    }
    task::Task<void> remove(RANGES::any_view<KeyType> keys)
        requires withErasable
    {
        return m_storage->remove(keys);
    }
};

}  // namespace bcos::storage2::any_storage