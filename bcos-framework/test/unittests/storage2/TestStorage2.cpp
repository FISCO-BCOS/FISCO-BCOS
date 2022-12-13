#include <bcos-framework/storage2/Storage2.h>
#include <bcos-task/Task.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;

class MockStorage : public bcos::storage2::StorageBase<MockStorage>
{
public:
    task::Task<void> impl_getRows(
        std::string_view tableName, InputKeys auto const& keys, OutputEntries auto& out)
    {
        for (auto& [key, entry] : RANGES::zip_view(keys, out))
        {
            auto keyPair = std::make_tuple(std::string(tableName), std::string(key));
            auto it = m_values.find(keyPair);
            if (it != m_values.end())
            {
                entry = it->second;
            }
        }

        co_return;
    }

    task::Task<void> impl_setRows(
        std::string_view tableName, InputKeys auto const& keys, InputEntries auto const& entries)
    {
        for (auto& [key, entry] : RANGES::zip_view(keys, entries))
        {
            auto keyPair = std::make_tuple(std::string(tableName), std::string(key));
            m_values.insert(std::make_pair(keyPair, entry));
        }

        co_return;
    }

    // template <Iterator IteratorType>
    // task::Task<IteratorType> impl_seek(std::string_view tableName, std::string_view key)
    // {
    //     co_return {};
    // }

    std::map<std::tuple<std::string, std::string>, storage::Entry> m_values;
};

