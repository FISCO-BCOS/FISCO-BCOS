#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include <optional>

using namespace bcos::storage;

TableInfo::ConstPtr StorageInterface::getSysTableInfo(std::string_view tableName)
{
    struct SystemTables
    {
        SystemTables()
        {
            tables.push_back(std::make_shared<TableInfo>(
                std::string(SYS_TABLES), std::vector<std::string>{SYS_TABLE_VALUE_FIELDS}));
        }

        std::vector<TableInfo::ConstPtr> tables;
    } static m_systemTables;

    if (tableName == SYS_TABLES)
    {
        return m_systemTables.tables[0];
    }

    return nullptr;
}

void StorageInterface::asyncCreateTable(std::string _tableName, std::string _valueFields,
    std::function<void(Error::UniquePtr, std::optional<Table>)> callback)
{
    asyncOpenTable(SYS_TABLES, [this, tableName = std::move(_tableName),
                                   callback = std::move(callback),
                                   valueFields = std::move(_valueFields)](
                                   auto&& error, auto&& sysTable) {
        if (error)
        {
            callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "Open sys_tables failed!", *error), {});
            return;
        }

        sysTable->asyncGetRow(tableName, [this, tableName, callback = std::move(callback),
                                             &sysTable, valueFields = std::move(valueFields)](
                                             auto&& error, auto&& entry) {
            if (error)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                             StorageError::ReadError, "Get table info row failed!", *error),
                    {});
                return;
            }

            if (entry)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(
                             StorageError::TableExists, "Table: " + tableName + " already exists"),
                    {});
                return;
            }

            auto tableEntry = sysTable->newEntry();
            tableEntry.setField(0, std::string(valueFields));

            sysTable->asyncSetRow(tableName, tableEntry,
                [this, callback, tableName, valueFields = std::move(valueFields)](auto&& error) {
                    if (error)
                    {
                        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                     -1, "Put table info into sys_tables failed!", *error),
                            {});
                        return;
                    }

                    std::vector<std::string> fields;
                    boost::split(fields, valueFields, boost::is_any_of(","));

                    auto tableInfo = std::make_shared<storage::TableInfo>(
                        std::move(tableName), std::move(fields));
                    auto table = Table(this, tableInfo);

                    callback(nullptr, std::make_optional(std::move(table)));
                });
        });
    });
}

void StorageInterface::asyncOpenTable(std::string_view tableName,
    std::function<void(Error::UniquePtr, std::optional<Table>)> callback)
{
    asyncGetTableInfo(tableName, [this, callback = std::move(callback)](
                                     Error::UniquePtr error, TableInfo::ConstPtr tableInfo) {
        if (error)
        {
            callback(std::move(error), std::nullopt);
            return;
        }

        if (tableInfo)
        {
            callback(nullptr, Table(this, tableInfo));
        }
        else
        {
            callback(nullptr, std::nullopt);
        }
    });
}

void StorageInterface::asyncGetTableInfo(
    std::string_view tableName, std::function<void(Error::UniquePtr, TableInfo::ConstPtr)> callback)
{
    auto sysTableInfo = getSysTableInfo(tableName);
    if (sysTableInfo)
    {
        callback(nullptr, std::move(sysTableInfo));

        return;
    }
    else
    {
        asyncOpenTable(
            SYS_TABLES, [callback = std::move(callback), tableName = std::string(tableName)](
                            Error::UniquePtr&& error, std::optional<Table>&& sysTable) {
                if (error)
                {
                    callback(std::move(error), {});
                    return;
                }

                if (!sysTable)
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(StorageError::SystemTableNotExists,
                                 "System table: " + std::string(SYS_TABLES) + " not found!"),
                        {});
                    return;
                }

                sysTable->asyncGetRow(tableName,
                    [tableName, callback = std::move(callback)](auto&& error, auto&& entry) {
                        if (error)
                        {
                            callback(std::move(error), {});
                            return;
                        }

                        if (!entry)
                        {
                            callback(nullptr, {});
                            return;
                        }

                        std::vector<std::string> fields;
                        boost::split(fields, entry->getField(0), boost::is_any_of(","));

                        auto tableInfo = std::make_shared<storage::TableInfo>(
                            std::move(tableName), std::move(fields));

                        callback(nullptr, std::move(tableInfo));
                    });
            });
    }
}