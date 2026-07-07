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
// TODO(#5312): MemEncode convention is fixed to std::function<void(bytesConstRef)>,
// so every encode() call constructs a std::function temporary.
// Future work: template the sink to eliminate type-erasure overhead,
// or add an owning std::string overload to decode for zero-copy.
PRO_DEF_MEM_DISPATCH(MemGetTypedPtr, getTypedPtr);
PRO_DEF_MEM_DISPATCH(MemTypeIndex, typeIndex);

struct AnyEntryFacade
  : pro::facade_builder ::add_convention<MemData, const char*() const noexcept>::add_convention<
        MemSize, size_t() const noexcept>::add_convention<MemStatus,
        EntryStatus() const noexcept>::add_convention<MemEncode,
        void(std::function<void(bytesConstRef)>) const>::add_convention<MemGetTypedPtr,
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

    void encode(std::function<void(bytesConstRef)> sink) const
    {
        sink(bytesConstRef(reinterpret_cast<const bcos::byte*>(data()), size()));
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

    void encode(std::function<void(bytesConstRef)> sink) const
    {
        sink(bytesConstRef(reinterpret_cast<const bcos::byte*>(data()), size()));
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

    void encode(std::function<void(bytesConstRef)> sink) const
    {
        sink(bytesConstRef(reinterpret_cast<const bcos::byte*>(data()), size()));
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

    void encode(std::function<void(bytesConstRef)> sink) const
    {
        sink(bytesConstRef(reinterpret_cast<const bcos::byte*>(data()), size()));
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

    void encode(std::function<void(bytesConstRef)>) const {}

    std::type_index typeIndex() const noexcept { return typeid(void); }
    const void* getTypedPtr() const noexcept { return nullptr; }
};

// ─── tag_invoke infrastructure ─────────────────────────────────────
// ADL anchor declared in bcos::storage.  Users must overload tag_invoke
// in their own namespaces to provide encode/decode for their types.
void tag_invoke();  // not defined — poison pill to prevent unqualified calls

// ─── Encode customization point object ────────────────────────────
// Requires: tag_invoke(encode_t, const T&, Sink) where Sink is callable
// with bytesConstRef, to be defined in T's associated namespace.
struct encode_t
{
    template <typename T, typename Sink>
    void operator()(const T& v, Sink&& sink) const
    {
        tag_invoke(*this, v, std::forward<Sink>(sink));
    }
};
inline constexpr encode_t encode{};

// ─── Decode customization point object ────────────────────────────
// Requires: tag_invoke(decode_t, std::type_identity<T>, bytesConstRef)
// to be defined in T's associated namespace.
struct decode_t
{
    template <typename T>
    T operator()(std::type_identity<T>, bytesConstRef data) const
    {
        return tag_invoke(*this, std::type_identity<T>{}, data);
    }
};
inline constexpr decode_t decode{};

// ─── Encodable concept ─────────────────────────────────────────────
// Satisfied when tag_invoke(encode_t, v, sink) and
// tag_invoke(decode_t, type_identity<T>{}, bytes) are well-formed.
template <typename T>
concept Encodable = requires(const T& v, bytesConstRef bytes) {
    {
        encode(v, [](bytesConstRef) {})
    } -> std::same_as<void>;
    { decode(std::type_identity<T>{}, bytes) } -> std::same_as<T>;
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
    // TODO(#5312): Typed entries are always MODIFIED→dirty(), which collides
    // with the commit path's hash-if-dirty logic.  Eventually degrade to byte
    // semantics: hash() should hash encoded bytes, setStatus() should
    // materialize encoded bytes before changing status.
    EntryStatus status() const noexcept { return ENTRY_MODIFIED; }

    void encode(std::function<void(bytesConstRef)> sink) const
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

    Entry(const Entry& other) : m_buffer(other.m_buffer)
    {
        // Propagate TYPED state so copies of typed entries hit the fast path.
        // LOCKED state is not copied — the source must not be under concurrent
        // mutation during copy (that would be UB regardless).
        if (other.m_decodeState.load(std::memory_order_acquire) == DECODE_TYPED)
            m_decodeState.store(DECODE_TYPED, std::memory_order_relaxed);
    }
    Entry(Entry&& other) noexcept : m_buffer(std::move(other.m_buffer))
    {
        if (other.m_decodeState.load(std::memory_order_acquire) == DECODE_TYPED)
            m_decodeState.store(DECODE_TYPED, std::memory_order_relaxed);
    }
    bcos::storage::Entry& operator=(const Entry& other)
    {
        m_buffer = other.m_buffer;
        // Propagate TYPED state; if other is BYTE we keep current state.
        if (other.m_decodeState.load(std::memory_order_acquire) == DECODE_TYPED)
            m_decodeState.store(DECODE_TYPED, std::memory_order_relaxed);
        return *this;
    }
    bcos::storage::Entry& operator=(Entry&& other) noexcept
    {
        m_buffer = std::move(other.m_buffer);
        if (other.m_decodeState.load(std::memory_order_acquire) == DECODE_TYPED)
            m_decodeState.store(DECODE_TYPED, std::memory_order_relaxed);
        return *this;
    }
    ~Entry() noexcept = default;

    // ── Accessors ──────────────────────────────────────────────────
    // NOTE: get()/data()/size() do not participate in the decode state
    // machine.  They read m_buffer directly without acquiring DECODE_LOCKED.
    // Concurrent get() + getTyped() (slow path) on the same Entry is a data
    // race — callers must ensure external synchronization or use getTyped()
    // exclusively once typed access begins.

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

    // Encode for persistence via the facade's encode convention.
    // Passes raw bytes through the sink callback, avoiding intermediate
    // string allocation compared to the old encodeToBytes().
    void encode(auto&& sink) const
    {
        if (!m_buffer.has_value())
            return;
        m_buffer->encode(std::forward<decltype(sink)>(sink));
    }
    // Reconstruct from raw bytes (no type tag needed — caller knows the type).
    static Entry decode(bytesConstRef data);
    // ── Status ─────────────────────────────────────────────────────

    Status status() const;
    void setStatus(Status status);
    bool dirty() const;

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
    // Tri-state atomic for lazy-decode in getTyped<T>().
    //
    // acquire-release ordering: store(DECODE_TYPED, release) pairs with
    // load(acquire)==DECODE_TYPED in the fast path, guaranteeing visibility
    // of m_buffer writes on weakly-ordered architectures.
    static constexpr int DECODE_BYTE = 0;    // m_buffer holds a byte model (unlocked)
    static constexpr int DECODE_LOCKED = 1;  // decode in progress — m_buffer being written
    static constexpr int DECODE_TYPED =
        2;  // m_buffer holds TypedHolderModel<T> (stable, immutable)
    mutable std::atomic<int> m_decodeState{DECODE_BYTE};

    // CAS-spin to acquire exclusive access to m_buffer.
    // Returns the state that was replaced: DECODE_BYTE or DECODE_TYPED.
    // Spins with backoff if another thread holds DECODE_LOCKED.
    int acquireDecodeLock() const
    {
        int spins = 0;
        while (true)
        {
            int expected = m_decodeState.load(std::memory_order_relaxed);
            if (expected != DECODE_LOCKED)
            {
                if (m_decodeState.compare_exchange_weak(expected, DECODE_LOCKED,
                        std::memory_order_acquire, std::memory_order_relaxed))
                {
                    return expected;
                }
                continue;
            }
            if (++spins > 64)
            {
                spins = 0;
                std::this_thread::yield();
            }
        }
    }

    // Unit-test accessor.
    friend const Holder& entryTestHolder(const Entry& e) noexcept { return e.m_buffer; }
};

// ─── Template implementations ──────────────────────────────────────

template <Encodable T>
void Entry::setTyped(T value)
{
    acquireDecodeLock();  // CAS BYTE→LOCKED or TYPED→LOCKED; spin-waits if LOCKED
    m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(TypedHolderModel<T>{std::move(value)});
    m_decodeState.store(DECODE_TYPED, std::memory_order_release);
}

template <Encodable T>
const T* Entry::getTyped() const
{
    if (!m_buffer.has_value())
    {
        return nullptr;
    }

    // ── Fast path: already typed, lock-free ───────────────────────
    // DECODE_TYPED: m_buffer is typed AND immutable (no concurrent
    // writer will ever modify it).  acquire-load synchronizes with the
    // release-store in setTyped() or the slow-path decode completion,
    // guaranteeing full visibility of the TypedHolderModel<T> inline data.
    if (m_decodeState.load(std::memory_order_acquire) == DECODE_TYPED)
    {
        auto* ptr = m_buffer->getTypedPtr();
        if (ptr != nullptr && m_buffer->typeIndex() == std::type_index(typeid(T)))
        {
            return static_cast<const T*>(ptr);
        }
        // Rare edge case: DECODE_TYPED but getTypedPtr() is null.
        // Can happen if set() overwrites a previously-typed entry
        // (m_buffer now holds bytes, state is stale).  Fall through
        // to the CAS loop which resets state→DECODE_BYTE and retries.
    }

    // ── Slow path: CAS-based synchronization ──────────────────────
    int prevState = acquireDecodeLock();

    // RAII guard: on exception (e.g. T ctor throws on corrupt data),
    // reset state to DECODE_BYTE so future callers can retry.
    struct StateGuard
    {
        std::atomic<int>* state;
        bool dismissed = false;
        ~StateGuard()
        {
            if (!dismissed) [[unlikely]]
                state->store(DECODE_BYTE, std::memory_order_release);
        }
    } guard{std::addressof(m_decodeState)};

    // Double-check m_buffer regardless of prevState.  Handles:
    // - Another thread finished decoding while we waited (prevState==TYPED).
    // - Copy/move of a typed entry where state is stale BYTE (prevState==BYTE
    //   but m_buffer already holds TypedHolderModel).
    if (const auto* ptr = m_buffer->getTypedPtr(); ptr != nullptr)
    {
        if (m_buffer->typeIndex() == std::type_index(typeid(T)))
        {
            guard.dismissed = true;
            m_decodeState.store(DECODE_TYPED, std::memory_order_release);
            return static_cast<const T*>(ptr);
        }
        // Typed, but wrong type — immutable.
        guard.dismissed = true;
        m_decodeState.store(DECODE_TYPED, std::memory_order_release);
        return nullptr;
    }

    // Not typed — must be byte-buffer.  Perform the lazy decode.
    auto view = get();
    if (view.empty())
    {
        guard.dismissed = true;
        m_decodeState.store(DECODE_BYTE, std::memory_order_release);
        return nullptr;
    }

    auto obj = bcos::storage::decode(std::type_identity<T>{},
        bytesConstRef(reinterpret_cast<const bcos::byte*>(view.data()), view.size()));
    m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(TypedHolderModel<T>{std::move(obj)});

    guard.dismissed = true;
    m_decodeState.store(DECODE_TYPED, std::memory_order_release);
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
