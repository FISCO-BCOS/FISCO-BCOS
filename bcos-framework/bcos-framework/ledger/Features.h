#pragma once
#include "../protocol/Protocol.h"
#include "../storage/Entry.h"
#include "../storage/StorageInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-task/Task.h"
#include <bcos-concepts/Exception.h>
#include <bcos-utilities/Ranges.h>
#include <boost/archive/binary_iarchive.hpp>
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
    // Use for storage key, do not change the enum name!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // At most 256 flag
    enum class Flag
    {
        bugfix_revert,  // https://github.com/FISCO-BCOS/FISCO-BCOS/issues/3629
        feature_sharding,
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

    void setToDefault(protocol::BlockVersion version)
    {
        if (version >= protocol::BlockVersion::V3_2_VERSION)
        {
            set(Flag::bugfix_revert);
        }

        if (version >= protocol::BlockVersion::V3_3_VERSION &&
            version <= protocol::BlockVersion::V3_4_VERSION)
        {
            set(Flag::feature_sharding);
        }
    }

    auto flags() const
    {
        return RANGES::views::iota(0LU, m_flags.size()) |
               RANGES::views::transform([this](size_t index) {
                   auto flag = magic_enum::enum_value<Flag>(index);
                   return std::make_tuple(flag, magic_enum::enum_name(flag), m_flags[index]);
               });
    }

    static auto featureKeys()
    {
        return RANGES::views::iota(0LU, magic_enum::enum_count<Flag>()) |
               RANGES::views::transform([](size_t index) {
                   auto flag = magic_enum::enum_value<Flag>(index);
                   return magic_enum::enum_name(flag);
               });
    }

    static task::Task<Features> readFeaturesFromStorage(
        storage::StorageInterface& storage, long blockNumber)
    {
        Features features;
        for (auto key : bcos::ledger::Features::featureKeys())
        {
            auto entry = co_await storage.coGetRow(ledger::SYS_CONFIG, key);
            if (entry)
            {
                auto [value, enableNumber] = entry->getObject<ledger::SystemConfigEntry>();
                if (blockNumber >= enableNumber)
                {
                    features.set(key);
                }
            }
        }
        co_return features;
    }
};

inline std::ostream& operator<<(std::ostream& stream, Features::Flag flag)
{
    return stream;
}

}  // namespace bcos::ledger
