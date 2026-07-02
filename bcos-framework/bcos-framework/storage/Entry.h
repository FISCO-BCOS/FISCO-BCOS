#pragma once

#include "Common.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <proxy/proxy.h>
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
#include <range/v3/range/concepts.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>

namespace bcos::storage
{

template <class T>
concept ByteBuffer = requires(const T& t) {
    { t.data() } -> std::convertible_to<const void*>;
    { t.size() } -> std::convertible_to<std::size_t>;
    requires sizeof(typename std::remove_cvref_t<T>::value_type) == 1;
};

// ─── View detection ────────────────────────────────────────────────
// Delegates to std::ranges::view which covers string_view, span, and any
// other standard or user-defined view that opts in via view_interface.
// Non-owning views → deep-copied; owning types → stored directly.
template <typename T>
constexpr bool IsByteBufferViewV = ::ranges::view_<std::remove_cvref_t<T>>;

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

// ─── Proxy-based type-erased entry facade ──────────────────────────
// Unified facade for both byte-buffer and typed models.
// Byte-buffer models: data/size/status return buffer content; encodeTo
//   returns a copy of raw bytes; getTypedPtr returns nullptr.
// Typed models: data returns nullptr; encodeTo calls T::encode();
//   getTypedPtr returns the concrete object pointer.

inline constexpr size_t MAX_PROXY_BUFFER_SIZE = 32;

PRO_DEF_MEM_DISPATCH(MemData, data);
PRO_DEF_MEM_DISPATCH(MemSize, size);
PRO_DEF_MEM_DISPATCH(MemStatus, status);
PRO_DEF_MEM_DISPATCH(MemEncodeTo, encodeTo);
PRO_DEF_MEM_DISPATCH(MemGetTypedPtr, getTypedPtr);

struct AnyEntryFacade
  : pro::facade_builder ::add_convention<MemData, const char*() const noexcept>::add_convention<
        MemSize, size_t() const noexcept>::add_convention<MemStatus, EntryStatus() const noexcept>::
        add_convention<MemEncodeTo, std::string() const>::add_convention<MemGetTypedPtr,
            void*() const noexcept>::support_copy<pro::constraint_level::nontrivial>::
            support_relocation<pro::constraint_level::nothrow>::support_destruction<
                pro::constraint_level::nothrow>::restrict_layout<MAX_PROXY_BUFFER_SIZE, 8>::build
{
};

// ─── Buffer models ─────────────────────────────────────────────────
// Every model implements the full AnyEntryFacade convention set.
// Byte-buffer models: data/size/status work as before; encodeTo returns
//   a copy of the raw bytes; typeTag returns RAW_BYTES; getTypedPtr returns nullptr.
// Typed models: data returns nullptr; encodeTo calls T::encode();
//   typeTag returns the tag for T; getTypedPtr returns the concrete object.

// Stores ≤31 bytes inline.
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
    [[nodiscard]] std::string encodeTo() const { return {data(), size()}; }
    [[nodiscard]] void* getTypedPtr() const noexcept { return nullptr; }
};

// Stores exactly 32 bytes inline.
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
    [[nodiscard]] std::string encodeTo() const { return {data(), size()}; }
    [[nodiscard]] void* getTypedPtr() const noexcept { return nullptr; }
};

// Adapts any ByteBuffer-conforming type T.
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
    [[nodiscard]] std::string encodeTo() const { return {data(), size()}; }
    [[nodiscard]] void* getTypedPtr() const noexcept { return nullptr; }
};

// Adapts a shared_ptr-wrapped ByteBuffer type.
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
    [[nodiscard]] std::string encodeTo() const { return {data(), size()}; }
    [[nodiscard]] void* getTypedPtr() const noexcept { return nullptr; }
};

// Deleted sentinel model.
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
    [[nodiscard]] std::string encodeTo() const { return {}; }
    [[nodiscard]] void* getTypedPtr() const noexcept { return nullptr; }
};

// ─── Typed holder model ────────────────────────────────────────────
// Stores a typed shared_ptr<void> (16 bytes) + encode function pointer
// (8 bytes) = 24 bytes inline.  Fits in 32-byte proxy SBO.
class TypedHolderModel
{
    std::shared_ptr<void> m_ptr;
    std::string (*m_encodeFn)(const void*);

public:
    TypedHolderModel(std::shared_ptr<void> ptr, std::string (*encodeFn)(const void*))
      : m_ptr(std::move(ptr)), m_encodeFn(encodeFn)
    {}
    [[nodiscard]] const char* data() const noexcept { return nullptr; }
    [[nodiscard]] size_t size() const noexcept { return 0; }
    [[nodiscard]] EntryStatus status() const noexcept { return ENTRY_MODIFIED; }
    [[nodiscard]] std::string encodeTo() const { return m_encodeFn(m_ptr.get()); }
    [[nodiscard]] void* getTypedPtr() const noexcept { return m_ptr.get(); }
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

    using Holder = pro::proxy<AnyEntryFacade>;

    Entry() = default;
    explicit Entry(auto input) { set(std::move(input)); }

    Entry(const Entry& other) = default;
    Entry(Entry&&) noexcept = default;
    bcos::storage::Entry& operator=(const Entry& other) = default;
    bcos::storage::Entry& operator=(Entry&&) noexcept = default;
    ~Entry() noexcept = default;

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
        if constexpr (IsByteBufferViewV<RawType>)
        {
            setImplCopy(reinterpret_cast<const char*>(value.data()), value.size());
        }
        else
        {
            auto sz = value.size();
            if (sz <= SMALL_SIZE)
                m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(
                    SmallBuffer<ENTRY_MODIFIED>{reinterpret_cast<const char*>(value.data()), sz});
            else if (sz == static_cast<decltype(sz)>(SMALL_SIZE + 1))
                m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(
                    Fixed32Buffer<ENTRY_MODIFIED>{reinterpret_cast<const char*>(value.data()), sz});
            else
                m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(
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
        m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(
            SharedBufferModel<T, ENTRY_MODIFIED>{std::move(value)});
    }

    // ── Typed storage API ──────────────────────────────────────────
    template <typename T>
    void setTyped(std::shared_ptr<T> ptr);

    template <typename T>
    T* getTyped() const;

    template <typename T>
    bool holdsType() const noexcept;

    // Encode for persistence via the facade's encodeTo convention.
    std::string encodeToBytes() const;

    // ── Status ─────────────────────────────────────────────────────

    Status status() const;
    void setStatus(Status status);
    bool dirty() const;

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

    bool valid() const;

    crypto::HashType hash(std::string_view table, std::string_view key,
        const bcos::crypto::Hash& hashImpl, uint32_t blockVersion) const
    {
        return hash(table, key, hashImpl, blockVersion, std::nullopt);
    }

    crypto::HashType hash(std::string_view table, std::string_view key,
        const bcos::crypto::Hash& hashImpl, uint32_t blockVersion,
        std::optional<bcos::ledger::Features> const& features) const;

private:
    void setImplCopy(const char* data, size_t sz);

    static Holder makeBuffer(EntryStatus es, const char* data, size_t sz);

    template <EntryStatus S>
    static Holder makeBufferImpl(const char* data, size_t sz)
    {
        if (sz <= SMALL_SIZE)
            return pro::make_proxy_inplace<AnyEntryFacade>(SmallBuffer<S>{data, sz});
        else if (sz == static_cast<size_t>(SMALL_SIZE + 1))
            return pro::make_proxy_inplace<AnyEntryFacade>(Fixed32Buffer<S>{data, sz});
        else
            return pro::make_proxy_inplace<AnyEntryFacade>(
                BufferModel<std::string, S>{std::string(data, sz)});
    }

    Holder m_buffer;

    // Unit-test accessor.
    friend const Holder& entryTestHolder(const Entry& e) noexcept { return e.m_buffer; }
};

// ─── Template implementations ──────────────────────────────────────

template <typename T>
void Entry::setTyped(std::shared_ptr<T> ptr)
{
    auto encodeFn = +[](const void* p) -> std::string {
        std::string out;
        static_cast<const T*>(p)->encode(out);
        return out;
    };
    m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(TypedHolderModel{std::move(ptr), encodeFn});
}

template <typename T>
T* Entry::getTyped() const
{
    if (!m_buffer.has_value())
        return nullptr;

    // If already a typed model, return the stored pointer.
    auto* ptr = m_buffer->getTypedPtr();
    if (ptr != nullptr)
        return static_cast<T*>(ptr);

    // Byte-buffer mode: lazy decode.
    auto view = get();
    if (view.empty())
        return nullptr;

    auto obj = std::make_shared<T>();
    obj->decode(bcos::bytesConstRef(reinterpret_cast<const bcos::byte*>(view.data()), view.size()));

    // Replace byte-buffer model with a typed model.
    auto encodeFn = +[](const void* p) -> std::string {
        std::string out;
        static_cast<const T*>(p)->encode(out);
        return out;
    };
    auto* self = const_cast<Entry*>(this);
    self->m_buffer =
        pro::make_proxy_inplace<AnyEntryFacade>(TypedHolderModel{std::move(obj), encodeFn});
    return static_cast<T*>(self->m_buffer->getTypedPtr());
}

template <typename T>
bool Entry::holdsType() const noexcept
{
    if (!m_buffer.has_value())
        return false;
    return m_buffer->getTypedPtr() != nullptr;
}
