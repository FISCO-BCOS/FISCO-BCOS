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
#include <initializer_list>
#include <type_traits>
#include <variant>

namespace bcos::storage
{

template <class Input>
concept EntryBufferInput =
    std::same_as<Input, std::string_view> || std::same_as<Input, std::string> ||
    std::same_as<Input, std::vector<char>> || std::same_as<Input, std::vector<unsigned char>>;

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

    constexpr static int32_t SMALL_SIZE = 32;
    constexpr static int32_t MEDIUM_SIZE = 64;
    constexpr static int32_t LARGE_SIZE = INT32_MAX;

    constexpr static int32_t ARCHIVE_FLAG =
        boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking;

    using SBOBuffer = std::array<char, SMALL_SIZE>;

    using ValueType = std::variant<SBOBuffer, std::string, std::vector<unsigned char>,
        std::vector<char>, std::shared_ptr<std::string>,
        std::shared_ptr<std::vector<unsigned char>>, std::shared_ptr<std::vector<char>>>;

    Entry() = default;
    explicit Entry(auto input) { set(std::move(input)); }

    Entry(const Entry&) = default;
    Entry(Entry&&) noexcept = default;
    bcos::storage::Entry& operator=(const Entry&) = default;
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

    std::string_view get() const& { return outputValueView(m_value); }

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

    void set(const char* pointer)
    {
        auto view = std::string_view(pointer, strlen(pointer));
        set(view);
    }

    void set(EntryBufferInput auto value)
    {
        auto view = inputValueView(value);
        m_size = view.size();
        if (m_size <= SMALL_SIZE)
        {
            if (m_value.index() != 0)
            {
                m_value = SBOBuffer();
            }

            std::copy_n(view.data(), view.size(), std::get<0>(m_value).data());
        }
        else
        {
            using ValueType = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<ValueType, std::string_view>)
            {
                set(std::string(view));
            }
            else
            {
                if (m_size <= MEDIUM_SIZE)
                {
                    m_value = std::move(value);
                }
                else
                {
                    m_value = std::make_shared<ValueType>(std::move(value));
                }
            }
        }

        m_status = MODIFIED;
    }
    template <EntryBufferInput T>
    void set(std::shared_ptr<T> value)
    {
        m_size = value->size();
        m_value = std::move(value);
        m_status = MODIFIED;
    }

    template <typename T>
    void setPointer(std::shared_ptr<T>&& value)
    {
        m_size = value->size();
        m_value = value;
    }

    Status status() const { return m_status; }

    void setStatus(Status status)
    {
        m_status = status;
        if (m_status == DELETED)
        {
            m_size = 0;
            m_value = std::string();
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

    auto&& exportFields()
    {
        m_size = 0;
        return std::move(m_value);
    }

    const char* data() const&
    {
        auto view = outputValueView(m_value);
        return view.data();
    }
    int32_t size() const { return m_size; }

    bool valid() const { return m_status == Status::NORMAL; }
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

private:
    [[nodiscard]] auto outputValueView(const ValueType& value) const& -> std::string_view
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

    template <typename T>
    [[nodiscard]] auto inputValueView(const T& value) const -> std::string_view
    {
        std::string_view view((const char*)value.data(), value.size());
        return view;
    }

    template <typename T>
    [[nodiscard]] auto inputValueView(const std::shared_ptr<T>& value) const -> std::string_view
    {
        std::string_view view((const char*)value->data(), value->size());
        return view;
    }

    ValueType m_value;                // should serialization
    int32_t m_size = 0;               // no need to serialization
    Status m_status = Status::EMPTY;  // should serialization
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