/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "StateStorageInterface.h"

using namespace bcos::storage;

Recoder::Change::Change(std::string _table, std::string _key, std::optional<Entry> _entry)
  : table(std::move(_table)), key(std::move(_key)), entry(std::move(_entry))
{}

void Recoder::log(Change&& change)
{
    m_changes.emplace_front(std::move(change));
}

std::list<Recoder::Change>::const_iterator Recoder::begin() const
{
    return m_changes.cbegin();
}

std::list<Recoder::Change>::const_iterator Recoder::end() const
{
    return m_changes.cend();
}

void Recoder::clear()
{
    m_changes.clear();
}

StateStorageInterface::StateStorageInterface(std::shared_ptr<StorageInterface> prev)
  : storage::TraverseStorageInterface(), m_prev(std::move(prev))
{}

std::optional<Table> StateStorageInterface::openTable(const std::string_view& tableView)
{
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> openPromise;
    asyncOpenTable(tableView, [&](auto&& error, auto&& table) {
        openPromise.set_value({std::move(error), std::move(table)});
    });

    auto [error, table] = openPromise.get_future().get();
    if (error)
    {
        BOOST_THROW_EXCEPTION(*error);
    }
    return table;
}

std::optional<Table> StateStorageInterface::createTable(
    std::string _tableName, std::string _valueFields)
{
    std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> createPromise;
    asyncCreateTable(
        std::move(_tableName), std::move(_valueFields),
        [&](Error::UniquePtr&& error, std::optional<Table>&& table) {
            createPromise.set_value({std::move(error), std::move(table)});
        });
    auto [error, table] = createPromise.get_future().get();
    if (error)
    {
        BOOST_THROW_EXCEPTION(*error);
    }
    return table;
}

std::pair<size_t, bcos::Error::Ptr> StateStorageInterface::count(
    const std::string_view& _table [[maybe_unused]])
{
    BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "Called interface count method"));
}

void StateStorageInterface::setPrev(std::shared_ptr<StorageInterface> prev)
{
    std::unique_lock<std::shared_mutex> lock(m_prevMutex);
    m_prev = std::move(prev);
}

void StateStorageInterface::setRecoder(typename Recoder::Ptr recoder)
{
    m_recoder.local().swap(recoder);
}

void StateStorageInterface::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
}