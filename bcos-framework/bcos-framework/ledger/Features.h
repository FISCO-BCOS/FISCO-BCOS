#pragma once
#include "../protocol/Protocol.h"
#include "../storage/Entry.h"
#include "../storage/LegacyStorageMethods.h"
#include "../storage2/Storage.h"
#include "bcos-concepts/Exception.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-task/Task.h"
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
struct PreconditionMismatchError : public bcos::Exception
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
        bugfix_statestorage_hash,
        bugfix_evm_create2_delegatecall_staticcall_codecopy,
        bugfix_event_log_order,
        bugfix_delegatecall_noaddr_return,
        bugfix_precompiled_codehash,
        feature_dmc2serial,
        feature_sharding,
        feature_rpbft,
        feature_paillier,
        feature_balance,
        feature_balance_precompiled,
        feature_balance_policy1,
    };

private:
    std::bitset<magic_enum::enum_count<Flag>()> m_flags;

public:
    static Flag string2Flag(std::string_view str)
    {
        auto value = magic_enum::enum_cast<Flag>(str);
        if (!value)
        {
            BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
        }
        return *value;
    }

    void validate(std::string flag) const
    {
        auto value = magic_enum::enum_cast<Flag>(flag);
        if (!value)
        {
            BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
        }

        validate(*value);
    }

    void validate(Flag flag) const
    {
        if ((flag == Flag::feature_balance_policy1 || flag == Flag::feature_balance_precompiled) &&
            !get(Flag::feature_balance))
        {
            BOOST_THROW_EXCEPTION(
                PreconditionMismatchError{} << errinfo_comment("must set feature_balance first"));
        }
    }

    bool get(Flag flag) const
    {
        auto index = magic_enum::enum_index(flag);
        if (!index)
        {
            BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
        }

        return m_flags[*index];
    }
    bool get(std::string_view flag) const { return get(string2Flag(flag)); }

    void set(Flag flag)
    {
        auto index = magic_enum::enum_index(flag);
        if (!index)
        {
            BOOST_THROW_EXCEPTION(NoSuchFeatureError{});
        }

        validate(flag);
        m_flags[*index] = true;
    }
    void set(std::string_view flag) { set(string2Flag(flag)); }

    void setToShardingDefault(protocol::BlockVersion version)
    {
        if (version >= protocol::BlockVersion::V3_3_VERSION &&
            version <= protocol::BlockVersion::V3_4_VERSION)
        {
            set(Flag::feature_sharding);
        }
    }

    void setToDefault(protocol::BlockVersion version)
    {
        if (version >= protocol::BlockVersion::V3_2_3_VERSION)
        {
            set(Flag::bugfix_revert);
        }
        if (version >= protocol::BlockVersion::V3_2_4_VERSION)
        {
            set(Flag::bugfix_statestorage_hash);
            set(Flag::bugfix_evm_create2_delegatecall_staticcall_codecopy);
        }

        // FIXME: Fork to 3.2.6
        if (version >= protocol::BlockVersion::V3_6_VERSION)
        {
            set(Flag::bugfix_event_log_order);
            set(Flag::bugfix_delegatecall_noaddr_return);
            set(Flag::bugfix_precompiled_codehash);
        }

        setToShardingDefault(version);
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

    task::Task<void> readFromStorage(auto& storage, long blockNumber)
    {
        for (auto key : bcos::ledger::Features::featureKeys())
        {
            auto entry =
                co_await storage2::readOne(storage, std::make_tuple(ledger::SYS_CONFIG, key));
            if (entry)
            {
                auto [value, enableNumber] = entry->template getObject<ledger::SystemConfigEntry>();
                if (blockNumber >= enableNumber)
                {
                    set(key);
                }
            }
        }
    }

    task::Task<void> writeToStorage(auto& storage, long blockNumber) const
    {
        for (auto [flag, name, value] : flags())
        {
            if (value)
            {
                storage::Entry entry;
                entry.setObject(
                    SystemConfigEntry{boost::lexical_cast<std::string>((int)value), blockNumber});
                co_await storage2::writeOne(
                    storage, std::make_tuple(ledger::SYS_CONFIG, name), std::move(entry));
            }
        }
    }
};

inline std::ostream& operator<<(std::ostream& stream, Features::Flag flag)
{
    stream << magic_enum::enum_name(flag);
    return stream;
}

}  // namespace bcos::ledger
