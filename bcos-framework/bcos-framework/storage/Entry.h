#pragma once

#include "Common.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-framework/protocol/Protocol.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <boost/archive/basic_archive.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cstdint>
#include <utility>

namespace bcos::storage
{

template <class BufferType>
concept IsBuffer = requires(BufferType buffer) {
    { buffer.data() };
    { buffer.size() } -> std::integral;
};
template <class BufferType>
concept IsBufferPointer = requires(BufferType buffer) {
    { buffer->data() };
    { buffer->size() } -> std::integral;
};

struct EmptyEntry
{
    EmptyEntry() = default;
    EmptyEntry(const EmptyEntry&) = default;
    EmptyEntry(EmptyEntry&&) noexcept = default;
    EmptyEntry& operator=(const EmptyEntry&) = default;
    EmptyEntry& operator=(EmptyEntry&&) noexcept = default;
    virtual ~EmptyEntry() noexcept = default;
    virtual const char* data() const { return nullptr; }
    virtual size_t size() const { return 0; }
    virtual void copy(void* target) const { new (target) EmptyEntry{}; }
    virtual void move(void* target) { new (target) EmptyEntry{}; }
};

constexpr static size_t IMPL_SIZE = 32;
constexpr static int32_t ARCHIVE_FLAG =
    boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking;

template <class Buffer>
    requires((IsBuffer<Buffer> || IsBufferPointer<Buffer>) && (sizeof(Buffer) <= IMPL_SIZE))
struct EntryImpl : public EmptyEntry
{
    Buffer m_buffer;

    explicit EntryImpl(std::in_place_t /*unused*/, auto&&... args)
      : m_buffer{std::forward<decltype(args)>(args)...}
    {}
    explicit EntryImpl(Buffer buffer) : m_buffer(std::move(buffer)) {}
    ~EntryImpl() noexcept override = default;

    const char* data() const override
    {
        if constexpr (IsBuffer<Buffer>)
        {
            return reinterpret_cast<const char*>(m_buffer.data());
        }
        else
        {
            return reinterpret_cast<const char*>(m_buffer->data());
        }
    }
    size_t size() const override
    {
        if constexpr (IsBuffer<Buffer>)
        {
            return m_buffer.size();
        }
        else
        {
            return m_buffer->size();
        }
    }
    static void init(void* target, auto&&... args)
    {
        new (target) EntryImpl{std::forward<decltype(args)>(args)...};
    }
    void copy(void* target) const override { new (target) EntryImpl{m_buffer}; }
    void move(void* target) override { new (target) EntryImpl{std::move(m_buffer)}; }
};

class Entry
{
public:
    enum Status : int8_t
    {
        NORMAL = 0,
        DELETED = 1,
        EMPTY = 2,
        MODIFIED = 3,  // dirty() can use status
    };

private:
    std::array<char, IMPL_SIZE + sizeof(EmptyEntry)> m_impl;
    Status m_status = Status::EMPTY;  // should serialization

    const EmptyEntry* impl() const { return reinterpret_cast<const EmptyEntry*>(m_impl.data()); }
    EmptyEntry* mutableImpl() { return reinterpret_cast<EmptyEntry*>(m_impl.data()); }
    void reset() noexcept { reinterpret_cast<EmptyEntry*>(m_impl.data())->~EmptyEntry(); }

public:
    Entry() { new (m_impl.data()) EmptyEntry{}; }
    template <class Buffer>
    explicit Entry(Buffer buffer) : m_status(MODIFIED)
    {
        EntryImpl<Buffer>::init(m_impl.data(), std::move(buffer));
    }
    template <class Buffer>
    explicit Entry(auto&&... args) : m_status(MODIFIED)
    {
        EntryImpl<Buffer>::init(
            m_impl.data(), std::in_place, std::forward<decltype(args)>(args)...);
    }
    explicit Entry(const char* str) : Entry(std::string_view(str)) {}
    explicit Entry(std::string_view view) : m_status(MODIFIED)
    {
        EntryImpl<std::string>::init(m_impl.data(), std::in_place, view);
    }

    Entry(const Entry& from)
    {
        if (this != &from)
        {
            from.impl()->copy(m_impl.data());
            m_status = from.m_status;
        }
    }
    Entry(Entry&& from) noexcept
    {
        if (this != &from)
        {
            from.mutableImpl()->move(m_impl.data());
            m_status = from.m_status;
        }
    }
    Entry& operator=(const Entry& from)
    {
        if (this != &from)
        {
            reset();
            from.impl()->copy(m_impl.data());
            m_status = from.m_status;
        }
        return *this;
    }
    Entry& operator=(Entry&& from) noexcept
    {
        if (this != &from)
        {
            reset();
            from.mutableImpl()->move(m_impl.data());
            m_status = from.m_status;
        }
        return *this;
    }
    ~Entry() noexcept { reset(); }

    std::string_view get() const&
    {
        const auto* ptr = impl();
        return {ptr->data(), ptr->size()};
    }
    template <class Buffer>
    void set(Buffer buffer)
    {
        reset();
        EntryImpl<Buffer>::init(m_impl.data(), std::move(buffer));
        m_status = MODIFIED;
    }
    template <class Buffer>
    void emplace(auto&&... args)
    {
        reset();
        EntryImpl<Buffer>::init(
            m_impl.data(), std::in_place, std::forward<decltype(args)>(args)...);
        m_status = MODIFIED;
    }
    void set(std::string_view view) { emplace<std::string>(view); }
    void set(const char* buffer) { set<std::string_view>(buffer); }

    template <typename Out, typename InputArchive = boost::archive::binary_iarchive,
        int flag = ARCHIVE_FLAG>
    void getObject(Out& out) const
    {
        auto view = get();
        boost::iostreams::stream<boost::iostreams::array_source> inputStream(
            view.data(), view.size());
        InputArchive archive(inputStream, flag);

        archive >> out;
    }

    template <typename Out, typename InputArchive = boost::archive::binary_iarchive,
        int flag = ARCHIVE_FLAG>
    Out getObject() const
    {
        Out out;
        getObject<Out, InputArchive, flag>(out);

        return out;
    }

    template <typename In, typename OutputArchive = boost::archive::binary_oarchive,
        int flag = ARCHIVE_FLAG>
    void setObject(const In& input)
    {
        std::string value;
        boost::iostreams::stream<boost::iostreams::back_insert_device<std::string>> outputStream(
            value);
        OutputArchive archive(outputStream, flag);

        archive << input;
        outputStream.flush();

        setField(0, std::move(value));
    }

    std::string_view getField(size_t index) const&
    {
        if (index > 0)
        {
            BOOST_THROW_EXCEPTION(
                BCOS_ERROR(-1, "Get field index: " + boost::lexical_cast<std::string>(index) +
                                   " failed, index out of range"));
        }

        return get();
    }

    template <typename T>
    void setField(size_t index, T&& input)
    {
        if (index > 0)
        {
            BOOST_THROW_EXCEPTION(
                BCOS_ERROR(-1, "Set field index: " + boost::lexical_cast<std::string>(index) +
                                   " failed, index out of range"));
        }

        set(std::forward<T>(input));
    }

    template <typename T>
    void setPointer(std::shared_ptr<T> value)
    {
        reset();
        EntryImpl<std::shared_ptr<T>>::init(m_impl.data(), std::move(value));
    }

    Status status() const { return m_status; }
    void setStatus(Status status)
    {
        m_status = status;
        if (m_status == DELETED)
        {
            reset();
            new (m_impl.data()) EmptyEntry{};
        }
    }
    bool dirty() const { return (m_status == MODIFIED || m_status == DELETED); }

    template <typename Input>
    void importFields(std::initializer_list<Input> values)
    {
        if (values.size() != 1)
        {
            BOOST_THROW_EXCEPTION(
                BCOS_ERROR(StorageError::UnknownEntryType, "Import fields not equal to 1"));
        }

        setField(0, std::move(*values.begin()));
    }

    const char* data() const& { return get().data(); }
    size_t size() const { return get().size(); }

    crypto::HashType hash(std::string_view table, std::string_view key,
        const bcos::crypto::Hash& hashImpl, uint32_t blockVersion) const
    {
        bcos::crypto::HashType entryHash(0);
        if (blockVersion >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            auto hasher = hashImpl.hasher();
            hasher.update(table);
            hasher.update(key);

            switch (m_status)
            {
            case MODIFIED:
            {
                auto data = get();
                hasher.update(data);
                hasher.final(entryHash);
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    STORAGE_LOG(TRACE)
                        << "Entry hash, dirty entry: " << table << " | " << toHex(key) << " | "
                        << toHex(data) << LOG_KV("hash", entryHash.abridged());
                }
                break;
            }
            case DELETED:
            {
                hasher.final(entryHash);
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    STORAGE_LOG(TRACE) << "Entry hash, deleted entry: " << table << " | "
                                       << toHex(key) << LOG_KV("hash", entryHash.abridged());
                }
                break;
            }
            default:
            {
                STORAGE_LOG(DEBUG) << "Entry hash, clean entry: " << table << " | " << toHex(key)
                                   << " | " << (int)m_status;
                break;
            }
            }
        }
        else
        {  // 3.0.0
            if (m_status == Entry::MODIFIED)
            {
                auto value = get();
                bcos::bytesConstRef ref((const bcos::byte*)value.data(), value.size());
                entryHash = hashImpl.hash(ref);
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    STORAGE_LOG(TRACE)
                        << "Entry Calc hash, dirty entry: " << table << " | " << toHex(key) << " | "
                        << toHex(value) << LOG_KV("hash", entryHash.abridged());
                }
            }
            else if (m_status == Entry::DELETED)
            {
                entryHash = bcos::crypto::HashType(0x1);
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    STORAGE_LOG(TRACE) << "Entry Calc hash, deleted entry: " << table << " | "
                                       << toHex(key) << LOG_KV("hash", entryHash.abridged());
                }
            }
        }
        return entryHash;
    }
};

}  // namespace bcos::storage

namespace boost::serialization
{
template <typename Archive, typename... Types>
void serialize(Archive& ar, std::tuple<Types...>& t, const unsigned int)
{
    std::apply([&](auto&... element) { ((ar & element), ...); }, t);
}
}  // namespace boost::serialization