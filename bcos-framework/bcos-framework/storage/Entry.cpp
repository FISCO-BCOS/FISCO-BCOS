#include "bcos-framework/storage/Entry.h"

#include "bcos-framework/protocol/Protocol.h"
#include <bcos-utilities/BoostLog.h>
#include <boost/endian/conversion.hpp>

namespace bcos::storage
{
std::string_view Entry::getField(size_t index) const&
{
    if (index > 0)
    {
        BOOST_THROW_EXCEPTION(
            BCOS_ERROR(-1, "Get field index: " + boost::lexical_cast<std::string>(index) +
                               " failed, index out of range"));
    }

    return get();
}

void Entry::setStatus(Status status)
{
    m_status = status;
    if (m_status == DELETED)
    {
        m_size = 0;
        m_value = std::string();
    }
}

const char* Entry::data() const&
{
    auto view = outputValueView(m_value);
    return view.data();
}

crypto::HashType Entry::hash(std::string_view table, std::string_view key,
    const bcos::crypto::Hash& hashImpl, uint32_t blockVersion,
    std::optional<bcos::ledger::Features> const& features) const
{
    const bool enableHashCollisionFix =
        features.has_value() &&
        features->get(bcos::ledger::Features::Flag::bugfix_statestorage_hash_v3_17);

    bcos::crypto::HashType entryHash(0);
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
        hasher.update(m_status);

        switch (m_status)
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
                               << " | " << static_cast<int>(m_status);
            break;
        }
        }
    }
    else if (blockVersion >= static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_1_VERSION))
    {
        auto hasher = hashImpl.hasher();
        hasher.update(table);
        hasher.update(key);

        switch (m_status)
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
                               << " | " << (int)m_status;
            break;
        }
        }
    }
    else
    {
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

auto Entry::outputValueView(const ValueType& value) const& -> std::string_view
{
    std::string_view view;
    std::visit(
        [this, &view](auto&& valueInside) {
            auto viewRaw = inputValueView(valueInside);
            view = std::string_view(viewRaw.data(), m_size);
        },
        value);
    return view;
}
}  // namespace bcos::storage