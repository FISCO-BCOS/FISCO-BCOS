#pragma once

#include <bcos-framework/storage2/Storage2.h>
#include <forward_list>

namespace bcos::transaction_executor
{

template <storage2::Storage PrevStorage>
class RollbackableStorage : public storage2::Storage2Base<RollbackableStorage<PrevStorage>>
{
public:
    RollbackableStorage(PrevStorage& prev) : m_prev(prev) {}

    task::Task<void> impl_getRows(std::string_view tableName, storage2::InputKeys auto const& keys,
        storage2::OutputEntries auto& out)
    {
        return m_prev.getRows(tableName, keys, out);
    }

    task::Task<void> impl_setRows(std::string_view tableName, storage2::InputKeys auto const& keys,
        storage2::InputEntries auto const& entries)
    {
        // Check the tableNames
        auto tableNameIt = m_tableNames.lower_bound(tableName);
        if (tableNameIt == m_tableNames.end() || *tableNameIt != tableName)
        {
            tableNameIt = m_tableNames.emplace_hint(tableNameIt, std::string(tableName));
        }

        for (auto const& [key, entry] : RANGES::zip_view(keys, entries))
        {
            auto keyView = concepts::bytebuffer::toView(key);
            // Try find old value
            auto record = m_records.emplace_front();
            record.tableName = *tableNameIt;
            record.key = std::string(keyView);

            auto tableKey = std::make_tuple(tableName, keyView);
            auto entryIt = m_writeValues.lower_bound(tableKey);
            if (entryIt != m_writeValues.end() && entryIt.first == tableKey)
            {
                record.oldValue.emplace(entryIt->second);
                entryIt->second = &record;
            }
            else
            {
                m_writeValues.emplace_hint(entryIt, std::make_pair(tableKey, &record));
            }
        }

        return m_prev.setRows(tableName, keys, entries);
    }

    void rollback()
    {
        for (auto& it : m_records)
        {}
    }

private:
    struct Record
    {
        std::string_view tableName;
        std::string key;
        storage2::OptionalEntry oldValue;
    };

    std::forward_list<Record> m_records;
    std::set<std::string, std::less<>> m_tableNames;
    std::map<std::tuple<std::string_view, std::string_view>, Record*, std::less<>> m_writeValues;
    PrevStorage& m_prev;
};

}  // namespace bcos::transaction_executor