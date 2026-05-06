#pragma once

#include "Common.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <boost/archive/basic_archive.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/lexical_cast.hpp>
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

constexpr static int32_t ARCHIVE_FLAG =
    boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking;

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

    std::string_view getField(size_t index) const&;

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

    template <typename T>
        requires(!EntryBufferInput<std::remove_cvref_t<T>> &&
                 std::convertible_to<T, std::string_view>)
    void set(T&& value)
    {
        set(std::string_view(std::forward<T>(value)));
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

    auto&& exportFields()
    {
        m_size = 0;
        return std::move(m_value);
    }

    const char* data() const&;
    int32_t size() const { return m_size; }

    bool valid() const { return m_status == Status::NORMAL; }
    crypto::HashType hash(std::string_view table, std::string_view key,
        const bcos::crypto::Hash& hashImpl, uint32_t blockVersion) const;

private:
    [[nodiscard]] auto outputValueView(const ValueType& value) const& -> std::string_view;

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