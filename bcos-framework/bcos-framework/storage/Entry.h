#pragma once

#include "Common.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <proxy/proxy.h>
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

// ─── Status enum at namespace scope ────────────────────────────────
// Defined before buffer models and facade so they can use it as a
// non-type template parameter and convention return type.  Values MUST
// match Entry::Status (kept for backward compatibility).

enum EntryStatus : int8_t
{
    ENTRY_NORMAL = 0,
    ENTRY_DELETED = 1,
    ENTRY_EMPTY = 2,
    ENTRY_MODIFIED = 3,  // dirty() can use status
};

// ─── Proxy-based type-erased byte-buffer facade ────────────────────
// Replaces the previous AnyHolder + AnyBufferVTableExt hand-rolled vtable
// with proxy's facade-builder, which achieves the same zero-virtual-overhead
// dispatch (data/size/status) through its own vtable mechanism.

// Maximum inline buffer size for the proxy's small-buffer optimization.
// Must accommodate the largest buffer model (currently BufferModel<T> / SmallBuffer etc.).
inline constexpr size_t MAX_PROXY_BUFFER_SIZE = 32;

PRO_DEF_MEM_DISPATCH(MemData, data);
PRO_DEF_MEM_DISPATCH(MemSize, size);
PRO_DEF_MEM_DISPATCH(MemStatus, status);

struct AnyBufferFacade
  : pro::facade_builder ::add_convention<MemData, const char*() const noexcept>::add_convention<
        MemSize, size_t() const noexcept>::add_convention<MemStatus,
        EntryStatus() const noexcept>::support_copy<pro::constraint_level::nontrivial>::
        support_relocation<pro::constraint_level::nothrow>::support_destruction<
            pro::constraint_level::nothrow>::restrict_layout<MAX_PROXY_BUFFER_SIZE, 8>::build
{
};

// Buffer models no longer need a common base class — proxy dispatches
// through the facade conventions above. Each model only needs data(),
// size() and status() member functions with the signatures declared in
// AnyBufferFacade.
//
// Every buffer model is templated on EntryStatus so the status is
// encoded in the *type* rather than stored as a runtime field.  This
// removes the 8-byte m_status member from Entry (reducing sizeof(Entry)
// from 48 → 40).  Status transitions reconstruct the proxy with a
// different template argument.

// Stores ≤31 bytes inline.  Repurposes 1 byte for the length so the
// total data-member footprint stays at 32 bytes (saving 8 B vs size_t).
template <EntryStatus S>
class SmallBuffer
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
    [[nodiscard]] EntryStatus status() const noexcept { return S; }
};

// Stores exactly 32 bytes inline — no size field needed.
template <EntryStatus S>
class Fixed32Buffer
{
    static constexpr size_t CAPACITY = 32;
    std::array<char, CAPACITY> m_buffer{};

public:
    Fixed32Buffer() = default;
    Fixed32Buffer(const char* data, size_t) { std::memcpy(m_buffer.data(), data, CAPACITY); }
    [[nodiscard]] const char* data() const noexcept { return m_buffer.data(); }
    [[nodiscard]] size_t size() const noexcept { return CAPACITY; }
    [[nodiscard]] EntryStatus status() const noexcept { return S; }
};

// Adapts any ByteBuffer-conforming type T (std::string, std::vector<char>,
// std::array<char,N>, etc.). Owns the value by move/copy.
template <ByteBuffer T, EntryStatus S>
    requires(sizeof(T) <= MAX_PROXY_BUFFER_SIZE)
class BufferModel
{
    T m_value;

public:
    explicit BufferModel(T value) : m_value(std::move(value)) {}
    [[nodiscard]] const char* data() const noexcept
    {
        return reinterpret_cast<const char*>(m_value.data());
    }
    [[nodiscard]] size_t size() const noexcept { return m_value.size(); }
    [[nodiscard]] EntryStatus status() const noexcept { return S; }
};

// Adapts a shared_ptr-wrapped ByteBuffer type. Shares ownership of the
// underlying buffer — no copy on clone().
template <ByteBuffer T, EntryStatus S>
class SharedBufferModel
{
    std::shared_ptr<T> m_ptr;

public:
    explicit SharedBufferModel(std::shared_ptr<T> ptr) : m_ptr(std::move(ptr)) {}
    [[nodiscard]] const char* data() const noexcept
    {
        return reinterpret_cast<const char*>(m_ptr->data());
    }
    [[nodiscard]] size_t size() const noexcept { return m_ptr->size(); }
    [[nodiscard]] EntryStatus status() const noexcept { return S; }
};

// ─── Deleted sentinel model ────────────────────────────────────────
// Encodes the DELETED status purely as a type tag — no data is stored.
// This avoids the value copy/move that the old setStatus(DELETED) path
// incurred.  The proxy still satisfies has_value()==true, but data()
// returns a pointer to an empty string and size() returns 0, so the
// observable behaviour is identical to the previous DELETED state.
class DeletedModel
{
public:
    [[nodiscard]] const char* data() const noexcept
    {
        static constexpr const char* empty = "";
        return empty;
    }
    [[nodiscard]] size_t size() const noexcept { return 0; }
    [[nodiscard]] EntryStatus status() const noexcept { return ENTRY_DELETED; }
};

class Entry
{
public:
    // Backward-compatible nested Status enum (values match EntryStatus)
    enum Status : int8_t
    {
        NORMAL = ENTRY_NORMAL,
        DELETED = ENTRY_DELETED,
        EMPTY = ENTRY_EMPTY,
        MODIFIED = ENTRY_MODIFIED,
    };

    constexpr static int32_t SMALL_SIZE = 31;
    static constexpr size_t INLINE_BUFFER_SIZE = 40;

    using Holder = pro::proxy<AnyBufferFacade>;

    Entry() = default;
    explicit Entry(auto input) { set(std::move(input)); }

    Entry(const Entry& other);
    Entry(Entry&&) noexcept = default;
    bcos::storage::Entry& operator=(const Entry& other);
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

    std::string_view get() const&;

    std::string_view getField(size_t index) const&;

    const char* data() const&;
    int32_t size() const;

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
                m_buffer = pro::make_proxy_inplace<AnyBufferFacade>(
                    SmallBuffer<ENTRY_MODIFIED>{reinterpret_cast<const char*>(value.data()), sz});
            else if (sz == static_cast<decltype(sz)>(SMALL_SIZE + 1))
                m_buffer = pro::make_proxy_inplace<AnyBufferFacade>(
                    Fixed32Buffer<ENTRY_MODIFIED>{reinterpret_cast<const char*>(value.data()), sz});
            else
                m_buffer = pro::make_proxy_inplace<AnyBufferFacade>(
                    BufferModel<RawType, ENTRY_MODIFIED>{std::forward<decltype(value)>(value)});
        }
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
        m_buffer = pro::make_proxy_inplace<AnyBufferFacade>(
            SharedBufferModel<T, ENTRY_MODIFIED>{std::move(value)});
    }

    template <typename T>
    void setPointer(std::shared_ptr<T>&& value)
    {
        set(std::move(value));
    }

    // ── Status ─────────────────────────────────────────────────────

    Status status() const
    {
        if (!m_buffer.has_value()) [[unlikely]]
            return Status::EMPTY;
        return static_cast<Status>(m_buffer.invoke(MemStatus{}));
    }
    void setStatus(Status status);
    bool dirty() const
    {
        if (!m_buffer.has_value()) [[unlikely]]
            return false;
        auto s = m_buffer.invoke(MemStatus{});
        return s == ENTRY_MODIFIED || s == ENTRY_DELETED;
    }

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

    bool valid() const
    {
        if (!m_buffer.has_value()) [[unlikely]]
            return false;
        return m_buffer.invoke(MemStatus{}) == ENTRY_NORMAL;
    }

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
            m_buffer =
                pro::make_proxy_inplace<AnyBufferFacade>(SmallBuffer<ENTRY_MODIFIED>{data, sz});
        else if (sz == static_cast<size_t>(SMALL_SIZE + 1))
            m_buffer =
                pro::make_proxy_inplace<AnyBufferFacade>(Fixed32Buffer<ENTRY_MODIFIED>{data, sz});
        else
            m_buffer = pro::make_proxy_inplace<AnyBufferFacade>(
                BufferModel<std::string, ENTRY_MODIFIED>{std::string(data, sz)});
    }

    // Helper: construct a buffer with the given status, preserving data.
    // Used by setStatus() for NORMAL ↔ MODIFIED transitions.
    static Holder makeBuffer(EntryStatus es, const char* data, size_t sz)
    {
        switch (es)
        {
        case ENTRY_NORMAL:
            return makeBufferImpl<ENTRY_NORMAL>(data, sz);
        case ENTRY_MODIFIED:
            return makeBufferImpl<ENTRY_MODIFIED>(data, sz);
        default:
            return Holder{};
        }
    }

    template <EntryStatus S>
    static Holder makeBufferImpl(const char* data, size_t sz)
    {
        if (sz <= SMALL_SIZE)
            return pro::make_proxy_inplace<AnyBufferFacade>(SmallBuffer<S>{data, sz});
        else if (sz == static_cast<size_t>(SMALL_SIZE + 1))
            return pro::make_proxy_inplace<AnyBufferFacade>(Fixed32Buffer<S>{data, sz});
        else
            return pro::make_proxy_inplace<AnyBufferFacade>(
                BufferModel<std::string, S>{std::string(data, sz)});
    }

    Holder m_buffer;

    // Unit-test accessor: exposes the internal proxy holder for inspection
    // (buffer model type can be verified via decltype and size checks).
    friend const Holder& entryTestHolder(const Entry& e) noexcept { return e.m_buffer; }
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