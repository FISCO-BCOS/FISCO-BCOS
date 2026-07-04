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
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <string>
#include <string_view>
#include <thread>
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
PRO_DEF_MEM_DISPATCH(MemEncode, encode);
PRO_DEF_MEM_DISPATCH(MemGetTypedPtr, getTypedPtr);
PRO_DEF_MEM_DISPATCH(MemTypeIndex, typeIndex);

struct AnyEntryFacade
  : pro::facade_builder ::add_convention<MemData, const char*() const noexcept>::add_convention<
        MemSize, size_t() const noexcept>::add_convention<MemStatus,
        EntryStatus() const noexcept>::add_convention<MemEncode,
        void(std::function<void(const uint8_t*, size_t)>) const>::add_convention<MemGetTypedPtr,
        const void*() const noexcept>::add_convention<MemTypeIndex,
        std::type_index() const noexcept>::support_copy<pro::constraint_level::nontrivial>::
        support_relocation<pro::constraint_level::nothrow>::support_destruction<
            pro::constraint_level::nothrow>::restrict_layout<MAX_PROXY_BUFFER_SIZE, 8>::build
{
};

// ─── Buffer models ─────────────────────────────────────────────────
// Every model implements the full AnyEntryFacade convention set.
// Byte-buffer models: data/size/status return buffer content; encode
//   feeds raw bytes to sink; getTypedPtr returns nullptr.
// Typed models: data returns nullptr; encode calls T::encode() then
//   feeds the result to sink; getTypedPtr returns the object pointer.

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
    const char* data() const noexcept { return m_buffer.data(); }
    size_t size() const noexcept { return m_size; }
    EntryStatus status() const noexcept { return S; }

    void encode(std::function<void(const uint8_t*, size_t)> sink) const
    {
        sink(reinterpret_cast<const uint8_t*>(data()), size());
    }

    std::type_index typeIndex() const noexcept { return typeid(void); }
    const void* getTypedPtr() const noexcept { return nullptr; }
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
    const char* data() const noexcept { return m_buffer.data(); }
    size_t size() const noexcept { return CAPACITY; }
    EntryStatus status() const noexcept { return S; }

    void encode(std::function<void(const uint8_t*, size_t)> sink) const
    {
        sink(reinterpret_cast<const uint8_t*>(data()), size());
    }

    std::type_index typeIndex() const noexcept { return typeid(void); }
    const void* getTypedPtr() const noexcept { return nullptr; }
};

// Adapts any ByteBuffer-conforming type T.
template <ByteBuffer T, EntryStatus S>
    requires(sizeof(T) <= MAX_PROXY_BUFFER_SIZE)
class BufferModel
{
    T m_value;

public:
    explicit BufferModel(T value) : m_value(std::move(value)) {}
    const char* data() const noexcept { return reinterpret_cast<const char*>(m_value.data()); }
    size_t size() const noexcept { return m_value.size(); }
    EntryStatus status() const noexcept { return S; }

    void encode(std::function<void(const uint8_t*, size_t)> sink) const
    {
        sink(reinterpret_cast<const uint8_t*>(data()), size());
    }

    std::type_index typeIndex() const noexcept { return typeid(void); }
    const void* getTypedPtr() const noexcept { return nullptr; }
};

// Adapts a shared_ptr-wrapped ByteBuffer type.
template <ByteBuffer T, EntryStatus S>
class SharedBufferModel
{
    std::shared_ptr<T> m_ptr;

public:
    explicit SharedBufferModel(std::shared_ptr<T> ptr) : m_ptr(std::move(ptr)) {}
    const char* data() const noexcept { return reinterpret_cast<const char*>(m_ptr->data()); }
    size_t size() const noexcept { return m_ptr->size(); }
    EntryStatus status() const noexcept { return S; }

    void encode(std::function<void(const uint8_t*, size_t)> sink) const
    {
        sink(reinterpret_cast<const uint8_t*>(data()), size());
    }

    std::type_index typeIndex() const noexcept { return typeid(void); }
    const void* getTypedPtr() const noexcept { return nullptr; }
};

// Deleted sentinel model.
class DeletedModel
{
public:
    const char* data() const noexcept
    {
        static constexpr const char* empty = "";
        return empty;
    }
    size_t size() const noexcept { return 0; }
    EntryStatus status() const noexcept { return ENTRY_DELETED; }

    void encode(std::function<void(const uint8_t*, size_t)>) const {}

    std::type_index typeIndex() const noexcept { return typeid(void); }
    const void* getTypedPtr() const noexcept { return nullptr; }
};

// ─── tag_invoke infrastructure ─────────────────────────────────────
// ADL anchor declared in bcos::storage.  Users must overload tag_invoke
// in their own namespaces to provide encode/decode for their types.
void tag_invoke();  // not defined — poison pill to prevent unqualified calls

// ─── Encode customization point object ────────────────────────────
// Requires: tag_invoke(encode_t, const T&, std::function<void(const uint8_t*, size_t)>)
// to be defined in T's associated namespace.
struct encode_t
{
    template <typename T>
    void operator()(const T& v, std::function<void(const uint8_t*, size_t)> sink) const
    {
        tag_invoke(*this, v, std::move(sink));
    }
};
inline constexpr encode_t encode{};

// ─── Decode customization point object ────────────────────────────
// Requires: tag_invoke(decode_t, std::type_identity<T>, const uint8_t*, size_t)
// to be defined in T's associated namespace.
struct decode_t
{
    template <typename T>
    T operator()(std::type_identity<T>, const uint8_t* data, size_t size) const
    {
        return tag_invoke(*this, std::type_identity<T>{}, data, size);
    }
};
inline constexpr decode_t decode{};

// ─── Encodable concept ─────────────────────────────────────────────
// Satisfied when tag_invoke(encode_t, v, sink) and
// tag_invoke(decode_t, type_identity<T>{}, data, size) are well-formed.
template <typename T>
concept Encodable = requires(const T& v, const uint8_t* data, size_t size) {
    {
        encode(v, std::declval<std::function<void(const uint8_t*, size_t)>>())
    } -> std::same_as<void>;
    { decode(std::type_identity<T>{}, data, size) } -> std::same_as<T>;
};

// ─── Typed holder model ────────────────────────────────────────────
// Stores T directly by value.  If sizeof(T) ≤ 32, the proxy keeps it
// inline (SBO); larger types are heap-allocated transparently.
template <Encodable T>
class TypedHolderModel
{
    T m_value;

public:
    explicit TypedHolderModel(T value) : m_value(std::move(value)) {}
    const char* data() const noexcept { return nullptr; }
    size_t size() const noexcept { return 0; }
    EntryStatus status() const noexcept { return ENTRY_MODIFIED; }

    void encode(std::function<void(const uint8_t*, size_t)> sink) const
    {
        bcos::storage::encode(m_value, std::move(sink));
    }
    const void* getTypedPtr() const noexcept { return &m_value; }
    std::type_index typeIndex() const noexcept { return typeid(T); }
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

    using Holder = pro::proxy<AnyEntryFacade>;

    Entry() = default;
    explicit Entry(auto input) { set(std::move(input)); }

    Entry(const Entry& other)
      : m_buffer(other.m_buffer)
    {}  // m_decodeLock default-initialized (cleared) — new object, independent state
    Entry(Entry&& other) noexcept : m_buffer(std::move(other.m_buffer)) {}
    bcos::storage::Entry& operator=(const Entry& other)
    {
        m_buffer = other.m_buffer;
        return *this;
    }
    bcos::storage::Entry& operator=(Entry&& other) noexcept
    {
        m_buffer = std::move(other.m_buffer);
        return *this;
    }
    ~Entry() noexcept = default;

    // ── Accessors ──────────────────────────────────────────────────

    std::string_view get() const&;
    const char* data() const&;
    int32_t size() const;

    // ── Mutators ───────────────────────────────────────────────────

    void set(ByteBuffer auto value)
    {
        using RawType = std::remove_cvref_t<decltype(value)>;
        if constexpr (IsByteBufferViewV<RawType>)
        {
            setImplCopy(reinterpret_cast<const char*>(value.data()), value.size());
        }
        else
        {
            auto valueSize = value.size();
            if (valueSize <= SMALL_SIZE)
            {
                m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(SmallBuffer<ENTRY_MODIFIED>{
                    reinterpret_cast<const char*>(value.data()), valueSize});
            }
            else if (valueSize == static_cast<decltype(valueSize)>(SMALL_SIZE + 1))
            {
                m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(Fixed32Buffer<ENTRY_MODIFIED>{
                    reinterpret_cast<const char*>(value.data()), valueSize});
            }
            else
            {
                m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(
                    BufferModel<RawType, ENTRY_MODIFIED>{std::forward<decltype(value)>(value)});
            }
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
    template <Encodable T>
    void setTyped(T value);

    template <Encodable T>
    const T* getTyped() const;

    template <Encodable T>
    bool holdsType() const noexcept;

    // Encode for persistence via the facade's encodeTo convention.
    std::string encodeToBytes() const;
    // Reconstruct from raw bytes (no type tag needed — caller knows the type).
    static Entry decodeFromBytes(std::string_view bytes);
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
        set(std::move(*values.begin()));
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
        {
            return pro::make_proxy_inplace<AnyEntryFacade>(SmallBuffer<S>{data, sz});
        }
        if (sz == static_cast<size_t>(SMALL_SIZE + 1))
        {
            return pro::make_proxy_inplace<AnyEntryFacade>(Fixed32Buffer<S>{data, sz});
        }
        return pro::make_proxy_inplace<AnyEntryFacade>(
            BufferModel<std::string, S>{std::string(data, sz)});
    }

    mutable Holder m_buffer;

    // ── Decode synchronization ────────────────────────────────────
    // Protects the lazy-decode transition in getTyped<T>().
    // Spinlock (atomic_flag): only 1-2 bytes overhead, and the
    // critical section is a single decode + proxy assignment —
    // spinning is cheaper than a futex wakeup.
    mutable std::atomic_flag m_decodeLock = ATOMIC_FLAG_INIT;

    // Unit-test accessor.
    friend const Holder& entryTestHolder(const Entry& e) noexcept { return e.m_buffer; }
};

// ─── Template implementations ──────────────────────────────────────

template <Encodable T>
void Entry::setTyped(T value)
{
    m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(TypedHolderModel<T>{std::move(value)});
}

template <Encodable T>
const T* Entry::getTyped() const
{
    if (!m_buffer.has_value())
    {
        return nullptr;
    }

    // ── Fast path: already typed, no lock ─────────────────────────
    // Acquire fence pairs with m_decodeLock.clear(release) in the
    // slow path to ensure visibility of TypedHolderModel<T> inline
    // data on weakly-ordered architectures (ARM, Power).
    std::atomic_thread_fence(std::memory_order_acquire);
    auto* ptr = m_buffer->getTypedPtr();
    if (ptr != nullptr && m_buffer->typeIndex() == std::type_index(typeid(T)))
    {
        return static_cast<const T*>(ptr);
    }

    // Typed model with a different type — refuse (type is immutable).
    if (ptr != nullptr)
    {
        return nullptr;
    }

    // ── Slow path: byte-buffer → typed, needs synchronization ─────
    // Spin until we acquire the lock, with backoff after 64 spins
    // to avoid burning CPU if the decoding thread is preempted.
    int spins = 0;
    while (m_decodeLock.test_and_set(std::memory_order_acquire))
    {
        if (++spins > 64)
        {
            spins = 0;
            std::this_thread::yield();
        }
    }

    // RAII spinlock guard: releases m_decodeLock on scope exit,
    // preventing lock leaks from early returns or exceptions.
    struct DecodeLockGuard
    {
        std::atomic_flag* lock;
        ~DecodeLockGuard() { lock->clear(std::memory_order_release); }
    } guard{std::addressof(m_decodeLock)};

    // Double-check: another thread may have decoded while we waited.
    ptr = m_buffer->getTypedPtr();
    if (ptr != nullptr && m_buffer->typeIndex() == std::type_index(typeid(T)))
    {
        return static_cast<const T*>(ptr);
    }

    // Typed model with a different type — refuse (type is immutable).
    if (ptr != nullptr)
    {
        return nullptr;
    }

    // Still byte-buffer — perform the decode.
    auto view = get();
    if (view.empty())
    {
        return nullptr;
    }

    auto obj =
        decode(std::type_identity<T>{}, reinterpret_cast<const uint8_t*>(view.data()), view.size());
    m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(TypedHolderModel<T>{std::move(obj)});
    return static_cast<const T*>(m_buffer->getTypedPtr());
}

template <Encodable T>
bool Entry::holdsType() const noexcept
{
    if (!m_buffer.has_value())
    {
        return false;
    }
    return m_buffer->typeIndex() == std::type_index(typeid(T));
}

}  // namespace bcos::storage
