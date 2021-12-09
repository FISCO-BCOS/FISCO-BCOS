#pragma once

#include "../../libutilities/Common.h"
#include "../../libutilities/Error.h"
#include "Common.h"
#include <boost/archive/basic_archive.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <type_traits>
#include <variant>

namespace bcos::storage
{
class Entry
{
public:
    enum Status : int8_t
    {
        NORMAL = 0,
        DELETED,
        PURGED,
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

    explicit Entry(TableInfo::ConstPtr) {}

    Entry(const Entry&) = default;
    Entry(Entry&&) noexcept = default;
    bcos::storage::Entry& operator=(const Entry&) = default;
    bcos::storage::Entry& operator=(Entry&&) noexcept = default;

    ~Entry() noexcept {}

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
    void setObject(const In& in)
    {
        std::string value;
        boost::iostreams::stream<boost::iostreams::back_insert_device<std::string>> outputStream(
            value);
        OutputArchive archive(outputStream, flag);

        archive << in;
        outputStream.flush();

        setField(0, std::move(value));
    }

    std::string_view get() const { return outputValueView(m_value); }

    std::string_view getField(size_t index) const
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

    void set(const char* p)
    {
        auto view = std::string_view(p, strlen(p));
        m_size = view.size();
        if (view.size() <= SMALL_SIZE)
        {
            if (m_value.index() != 0)
            {
                m_value = SBOBuffer();
            }

            std::copy_n(view.data(), view.size(), std::get<0>(m_value).data());
            m_dirty = true;
        }
        else
        {
            set(std::string(view));
        }
    }

    template <typename Input>
    void set(Input value)
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
        else if (m_size <= MEDIUM_SIZE)
        {
            m_value = std::move(value);
        }
        else
        {
            m_value = std::make_shared<Input>(std::move(value));
        }

        m_dirty = true;
    }

    Status status() const { return m_status; }

    void setStatus(Status status)
    {
        m_status = status;
        m_dirty = true;
    }

    bool dirty() const { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }

    int32_t size() const { return m_size; }

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

    bool valid() const { return m_status == Status::NORMAL; }

private:
    std::string_view outputValueView(const ValueType& value) const
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
    std::string_view inputValueView(const T& value) const
    {
        std::string_view view((const char*)value.data(), value.size());
        return view;
    }

    template <typename T>
    std::string_view inputValueView(const std::shared_ptr<T>& value) const
    {
        std::string_view view((const char*)value->data(), value->size());
        return view;
    }

    ValueType m_value;                 // should serialization
    int32_t m_size = 0;                // no need to serialization
    Status m_status = Status::NORMAL;  // should serialization
    bool m_dirty = false;              // no need to serialization
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