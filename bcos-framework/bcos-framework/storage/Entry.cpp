#include "bcos-framework/storage/Entry.h"

#include "bcos-framework/protocol/Protocol.h"
#include <bcos-utilities/BoostLog.h>

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
                STORAGE_LOG(TRACE) << "Entry hash, deleted entry: " << table << " | "
                                   << toHex(key) << LOG_KV("hash", entryHash.abridged());
            }
            break;
        }
        default:
        {
            STORAGE_LOG(DEBUG)
                << "Entry hash, clean entry: " << table << " | " << toHex(key) << " | "
                << (int)m_status;
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