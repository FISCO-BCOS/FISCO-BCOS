#pragma once
#include <bcos-concepts/Exception.h>
#include <bcos-utilities/Ranges.h>
#include <boost/throw_exception.hpp>
#include <array>
#include <bitset>
#include <magic_enum.hpp>

namespace bcos::ledger
{

struct NoSuchFeatureError : public bcos::error::Exception
{
};

class Features
{
public:
    // Use for storage key, do not change the enum name!
    // At most 256 flag
    enum class Flag
    {
        FEATURE_BUGFIX_REVERT,
    };

private:
    std::bitset<magic_enum::enum_count<Flag>()> m_flags;

public:
    bool get(Flag flag) const
    {
        auto index = magic_enum::enum_index(flag);
        if (!index)
        {
            BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
        }
        return m_flags[*index];
    }
    bool get(std::string_view flag) const
    {
        auto value = magic_enum::enum_cast<Flag>(flag);
        if (!value)
        {
            BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
        }
        return get(*value);
    }

    void set(Flag flag)
    {
        auto index = magic_enum::enum_index(flag);
        if (!index)
        {
            BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
        }
        m_flags[*index] = true;
    }
    void set(std::string_view flag)
    {
        auto value = magic_enum::enum_cast<Flag>(flag);
        if (!value)
        {
            BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
        }
        set(*value);
    }

    void setToDefault() { set(Flag::FEATURE_BUGFIX_REVERT); }

    auto flags() const
    {
        return RANGES::views::iota(0LU, m_flags.size()) |
               RANGES::views::transform([this](size_t index) {
                   auto flag = magic_enum::enum_value<Flag>(index);
                   return std::make_tuple(flag, magic_enum::enum_name(flag), m_flags[index]);
               });
    }
};

std::ostream& operator<<(std::ostream& stream, Features::Flag flag)
{
    return stream;
}

}  // namespace bcos::ledger