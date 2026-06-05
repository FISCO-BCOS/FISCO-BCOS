#pragma once

#include "Common.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/AnyHolder.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <boost/archive/basic_archive.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <optional>
#include <type_traits>
namespace bcos::storage
{

template <class T>
concept ByteBuffer = requires(const T& t) {
    { t.data() } -> std::convertible_to<const void*>;
    { t.size() } -> std::convertible_to<std::size_t>;
    requires sizeof(typename std::remove_cvref_t<T>::value_type) == 1;
};

constexpr static int32_t ARCHIVE_FLAG =
    boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking;

// ─── Type-erased byte-buffer interface ───────────────────────────────
// AnyBuffer is a non-virtual base tag-type. All dispatch (data/size/destroy)
// goes through AnyHolder's VTable, extended via AnyBufferVTableExt below.
// This eliminates one vtable pointer per Entry (AnyHolder and the buffer
// model now share a single vtable pointer).

// VTable extension that embeds data() / size() function pointers into
// AnyHolder's vtable so AnyBuffer does not need its own virtual methods.
struct AnyBufferVTableExt
{
    const char* (*data)(const void* obj) noexcept = nullptr;
    size_t (*size)(const void* obj) noexcept = nullptr;

    // Called by AnyHolder::getVTableFor<HoldType>() to fill these slots.
    template <typename HoldType>
    static void fillSlots(AnyBufferVTableExt& self)
    {
        self.data = [](const void* obj) noexcept -> const char* {
            return static_cast<const HoldType*>(obj)->data();
        };
        self.size = [](const void* obj) noexcept -> size_t {
            return static_cast<const HoldType*>(obj)->size();
        };
    }
};

class AnyBuffer
{
public:
    // Non-virtual destructor — safe because destruction is always routed
    // through AnyHolder's VTable (which knows the concrete type).
    ~AnyBuffer() = default;
    // data() / size() are non-virtual — dispatch through AnyHolder VTable.
};

// Stores ≤31 bytes inline.  Repurposes 1 byte for the length so the
// total data-member footprint stays at 32 bytes (saving 8 B vs size_t).
class SmallBuffer : public AnyBuffer
{
    static constexpr size_t CAPACITY = 31;
    std::array<char, CAPACITY> m_buffer{};
    uint8_t m_size = 0;

public:
    SmallBuffer() = default;
    SmallBuffer(const char* data, size_t size) : m_size(static_cast<uint8_t>(size))
    {
        std::memcpy(m_buffer.data(), data, size);
    }
    [[nodiscard]] const char* data() const noexcept { return m_buffer.data(); }
    [[nodiscard]] size_t size() const noexcept { return m_size; }
};

// Stores exactly 32 bytes inline — no size field needed.
class Fixed32Buffer : public AnyBuffer
{
    static constexpr size_t CAPACITY = 32;
    std::array<char, CAPACITY> m_buffer{};

public:
    Fixed32Buffer() = default;
    Fixed32Buffer(const char* data, size_t) { std::memcpy(m_buffer.data(), data, CAPACITY); }
    [[nodiscard]] const char* data() const noexcept { return m_buffer.data(); }
    [[nodiscard]] size_t size() const noexcept { return CAPACITY; }
};

// Adapts any ByteBuffer-conforming type T (std::string, std::vector<char>,
// std::array<char,N>, etc.). Owns the value by move/copy.
template <ByteBuffer T>
class BufferModel : public AnyBuffer
{
    T m_value;

public:
    explicit BufferModel(T value) : m_value(std::move(value)) {}
    [[nodiscard]] const char* data() const noexcept
    {
        return reinterpret_cast<const char*>(m_value.data());
    }
    [[nodiscard]] size_t size() const noexcept { return m_value.size(); }
};

// Adapts a shared_ptr-wrapped ByteBuffer type. Shares ownership of the
// underlying buffer — no copy on clone().
template <ByteBuffer T>
class SharedBufferModel : public AnyBuffer
{
    std::shared_ptr<T> m_ptr;

public:
    explicit SharedBufferModel(std::shared_ptr<T> ptr) : m_ptr(std::move(ptr)) {}
    [[nodiscard]] const char* data() const noexcept
    {
        return reinterpret_cast<const char*>(m_ptr->data());
    }
    [[nodiscard]] size_t size() const noexcept { return m_ptr->size(); }
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

    constexpr static int32_t SMALL_SIZE = 31;
    static constexpr size_t INLINE_BUFFER_SIZE = 40;

    using Holder = bcos::AnyHolder<AnyBuffer, INLINE_BUFFER_SIZE, true, true, AnyBufferVTableExt>;

    Entry() = default;
    explicit Entry(auto input) { set(std::move(input)); }

    Entry(const Entry& other) : m_status(other.m_status)
    {
        if (other.m_buffer)
            m_buffer = other.m_buffer;
    }
    Entry(Entry&&) noexcept = default;
    bcos::storage::Entry& operator=(const Entry& other)
    {
        if (this != &other)
        {
            m_buffer = {};
            m_status = other.m_status;
            if (other.m_buffer)
                m_buffer = other.m_buffer;
        }
        return *this;
    }
    bcos::storage::Entry& operator=(Entry&&) noexcept = default;
    ~Entry() noexcept = default;

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

    // ── Accessors ──────────────────────────────────────────────────

    std::string_view get() const&
    {
        if (!m_buffer) [[unlikely]]
            return {};
        return {m_buffer.vtableExt()->data(m_buffer.get()),
            m_buffer.vtableExt()->size(m_buffer.get())};
    }

    std::string_view getField(size_t index) const&;

    const char* data() const&
    {
        if (!m_buffer) [[unlikely]]
            return "";
        return m_buffer.vtableExt()->data(m_buffer.get());
    }
    int32_t size() const
    {
        return m_buffer ? static_cast<int32_t>(m_buffer.vtableExt()->size(m_buffer.get())) : 0;
    }

    // ── Mutators ───────────────────────────────────────────────────

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

    void set(ByteBuffer auto value)
    {
        using RawType = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<RawType, std::string_view>)
        {
            setImplCopy(value.data(), value.size());
        }
        else
        {
            auto sz = value.size();
            if (sz <= SMALL_SIZE)
                m_buffer = Holder(
                    bcos::InPlace<SmallBuffer>{}, reinterpret_cast<const char*>(value.data()), sz);
            else if (sz == static_cast<decltype(sz)>(SMALL_SIZE + 1))
                m_buffer = Holder(bcos::InPlace<Fixed32Buffer>{},
                    reinterpret_cast<const char*>(value.data()), sz);
            else
                m_buffer = Holder(
                    bcos::InPlace<BufferModel<RawType>>{}, std::forward<decltype(value)>(value));
        }
        m_status = MODIFIED;
    }

    template <typename T>
        requires(!ByteBuffer<std::remove_cvref_t<T>> && std::convertible_to<T, std::string_view>)
    void set(T&& value)
    {
        set(std::string_view(std::forward<T>(value)));
    }

    template <ByteBuffer T>
    void set(std::shared_ptr<T> value)
    {
        m_buffer = Holder(bcos::InPlace<SharedBufferModel<T>>{}, std::move(value));
        m_status = MODIFIED;
    }

    template <typename T>
    void setPointer(std::shared_ptr<T>&& value)
    {
        set(std::move(value));
    }

    // ── Status ─────────────────────────────────────────────────────

    Status status() const { return m_status; }
    void setStatus(Status status);
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

    bool valid() const { return m_status == Status::NORMAL; }

    crypto::HashType hash(std::string_view table, std::string_view key,
        const bcos::crypto::Hash& hashImpl, uint32_t blockVersion) const
    {
        return hash(table, key, hashImpl, blockVersion, std::nullopt);
    }

    crypto::HashType hash(std::string_view table, std::string_view key,
        const bcos::crypto::Hash& hashImpl, uint32_t blockVersion,
        std::optional<bcos::ledger::Features> const& features) const;

private:
    void setImplCopy(const char* data, size_t sz)
    {
        if (sz <= SMALL_SIZE)
            m_buffer = Holder(bcos::InPlace<SmallBuffer>{}, data, sz);
        else if (sz == static_cast<size_t>(SMALL_SIZE + 1))
            m_buffer = Holder(bcos::InPlace<Fixed32Buffer>{}, data, sz);
        else
            m_buffer = Holder(bcos::InPlace<BufferModel<std::string>>{}, std::string(data, sz));
    }

    Holder m_buffer;
    Status m_status = Status::EMPTY;
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