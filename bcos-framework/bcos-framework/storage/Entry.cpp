#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/protocol/Protocol.h"
#include <bcos-utilities/BoostLog.h>
#include <boost/endian/conversion.hpp>

namespace bcos::storage
{
DERIVE_BCOS_EXCEPTION(TypedEntryStatusChange);
DERIVE_BCOS_EXCEPTION(TypedEntryHashCall);

std::string_view Entry::get() const&
{
    if (!m_buffer.has_value()) [[unlikely]]
        return {};
    return {m_buffer->data(), m_buffer->size()};
}

const char* Entry::data() const&
{
    if (!m_buffer.has_value()) [[unlikely]]
        return "";
    return m_buffer->data();
}

int32_t Entry::size() const
{
    return m_buffer.has_value() ? static_cast<int32_t>(m_buffer->size()) : 0;
}

Entry::Status Entry::status() const
{
    if (!m_buffer.has_value()) [[unlikely]]
        return Status::EMPTY;
    return static_cast<Status>(m_buffer->status());
}

void Entry::setStatus(Status status)
{
    // Typed entries are immutable — refuse status mutation.
    if (m_buffer.has_value() && m_buffer->getTypedPtr() != nullptr)
    {
        BOOST_THROW_EXCEPTION(TypedEntryStatusChange{});
    }

    auto cur = this->status();
    if (cur == status)
        return;

    if (status == DELETED)
    {
        m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(DeletedModel{});
    }
    else if (status == EMPTY)
    {
        m_buffer = Holder{};
    }
    else
    {
        // NORMAL or MODIFIED: preserve data, change status tag.
        if (m_buffer.has_value())
        {
            auto view = get();
            m_buffer = makeBuffer(static_cast<EntryStatus>(status), view.data(), view.size());
        }
        else
        {
            m_buffer = makeBuffer(static_cast<EntryStatus>(status), "", 0);
        }
    }
}

bool Entry::dirty() const
{
    if (!m_buffer.has_value()) [[unlikely]]
        return false;
    auto s = m_buffer->status();
    return s == ENTRY_MODIFIED || s == ENTRY_DELETED;
}

bool Entry::valid() const
{
    if (!m_buffer.has_value()) [[unlikely]]
        return false;
    return m_buffer->status() == ENTRY_NORMAL;
}

void Entry::setImplCopy(const char* data, size_t sz)
{
    if (sz <= SMALL_SIZE)
        m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(SmallBuffer<ENTRY_MODIFIED>{data, sz});
    else if (sz == static_cast<size_t>(SMALL_SIZE + 1))
        m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(Fixed32Buffer<ENTRY_MODIFIED>{data, sz});
    else
        m_buffer = pro::make_proxy_inplace<AnyEntryFacade>(
            BufferModel<std::string, ENTRY_MODIFIED>{std::string(data, sz)});
}

Entry::Holder Entry::makeBuffer(EntryStatus es, const char* data, size_t sz)
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

std::string Entry::encodeToBytes() const
{
    if (!m_buffer.has_value())
        return {};
    std::string result;
    m_buffer->encode([&result](const uint8_t* data, size_t size) {
        result.append(reinterpret_cast<const char*>(data), size);
    });
    return result;
}

/* static */ Entry Entry::decodeFromBytes(std::string_view bytes)
{
    Entry entry;
    entry.set(bytes);
    return entry;
}

crypto::HashType Entry::hash(std::string_view table, std::string_view key,
    const bcos::crypto::Hash& hashImpl, uint32_t blockVersion,
    std::optional<bcos::ledger::Features> const& features) const
{
    if (m_buffer.has_value() && m_buffer->getTypedPtr() != nullptr)
    {
        BOOST_THROW_EXCEPTION(TypedEntryHashCall{});
    }

    const bool enableHashCollisionFix =
        features.has_value() &&
        features->get(bcos::ledger::Features::Flag::bugfix_statestorage_hash_v3_17);

    bcos::crypto::HashType entryHash(0);
    const auto s = status();
    if (enableHashCollisionFix)
    {
        // FIB-99: Length-prefixed, status-aware hashing to prevent boundary
        // ambiguity and status ambiguity collisions in state root calculation.
        // Gated by Features::Flag::bugfix_statestorage_hash_v3_17 (activated at V3_17_0),
        // not by blockVersion, so the fix follows the bugfix-flag semantic.
        auto hasher = hashImpl.hasher();
        // FIB-99 preimage format (fixed across platforms):
        //   u32be(tableLen) || table || u32be(keyLen) || key || i8(status) || [data if MODIFIED]
        // Use explicit fixed-width types so the hash is independent of sizeof(size_t),
        // and normalize length prefixes to big-endian so the digest is identical on
        // little-endian and big-endian hosts (matches the convention used in
        // bcos-tars-protocol TarsHashable.h and bcos-sealer VRFBasedSealer.cpp).
        const auto tableLenBE = boost::endian::native_to_big(static_cast<uint32_t>(table.size()));
        const auto keyLenBE = boost::endian::native_to_big(static_cast<uint32_t>(key.size()));
        hasher.update(tableLenBE);
        hasher.update(table);
        hasher.update(keyLenBE);
        hasher.update(key);
        // Entry status (int8_t) distinguishes DELETED from MODIFIED-with-empty-value;
        // single-byte field, no endianness conversion needed.
        hasher.update(static_cast<int8_t>(s));

        switch (s)
        {
        case MODIFIED:
        {
            const auto data = get();
            hasher.update(data);
            hasher.final(entryHash);
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                STORAGE_LOG(TRACE)
                    << "Entry hash v3.17+, dirty entry: " << table << " | " << toHex(key) << " | "
                    << toHex(data) << LOG_KV("hash", entryHash.abridged());
            }
            break;
        }
        case DELETED:
        {
            hasher.final(entryHash);
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                STORAGE_LOG(TRACE) << "Entry hash v3.17+, deleted entry: " << table << " | "
                                   << toHex(key) << LOG_KV("hash", entryHash.abridged());
            }
            break;
        }
        default:
        {
            STORAGE_LOG(DEBUG) << "Entry hash v3.17+, clean entry: " << table << " | " << toHex(key)
                               << " | " << static_cast<int>(s);
            break;
        }
        }
    }
    else if (blockVersion >= static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION))
    {
        auto hasher = hashImpl.hasher();
        hasher.update(table);
        hasher.update(key);

        switch (s)
        {
        case MODIFIED:
        {
            auto dataView = get();
            hasher.update(dataView);
            hasher.final(entryHash);
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                STORAGE_LOG(TRACE)
                    << "Entry hash, dirty entry: " << table << " | " << toHex(key) << " | "
                    << toHex(dataView) << LOG_KV("hash", entryHash.abridged());
            }
            break;
        }
        case DELETED:
        {
            hasher.final(entryHash);
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                STORAGE_LOG(TRACE) << "Entry hash, deleted entry: " << table << " | " << toHex(key)
                                   << LOG_KV("hash", entryHash.abridged());
            }
            break;
        }
        default:
        {
            STORAGE_LOG(DEBUG) << "Entry hash, clean entry: " << table << " | " << toHex(key)
                               << " | " << static_cast<int>(s);
            break;
        }
        }
    }
    else
    {
        if (s == Entry::MODIFIED)
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
        else if (s == Entry::DELETED)
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

}  // namespace bcos::storage