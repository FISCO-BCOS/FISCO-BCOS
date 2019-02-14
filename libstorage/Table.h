/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

#pragma once

#include "Common.h"
#include <libdevcore/Address.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_vector.h>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>

namespace dev
{
namespace storage
{
struct TableInfo : public std::enable_shared_from_this<TableInfo>
{
    typedef std::shared_ptr<TableInfo> Ptr;

    std::string name;
    std::string key;
    std::vector<std::string> fields;
    std::vector<Address> authorizedAddress;
};

struct AccessOptions : public std::enable_shared_from_this<AccessOptions>
{
    typedef std::shared_ptr<AccessOptions> Ptr;
    AccessOptions() = default;
    AccessOptions(Address _origin) { origin = _origin; }
    Address origin;
};

class Entry : public std::enable_shared_from_this<Entry>
{
public:
    struct MyHashCompare
    {
        static size_t hash(const std::string& x)
        {
            size_t h = 0;
            for (const char* s = x.c_str(); *s; ++s)
                h = (h * 17) ^ *s;
            return h;
        }
        //! True if strings are equal
        static bool equal(const std::string& x, const std::string& y) { return x == y; }
    };

    typedef std::shared_ptr<Entry> Ptr;

    enum Status
    {
        NORMAL = 0,
        DELETED
    };

    Entry();
    virtual ~Entry() {}

    virtual std::string getField(const std::string& key) const;
    virtual void setField(const std::string& key, const std::string& value);
    virtual std::map<std::string, std::string>* fields();

    virtual uint32_t getStatus();
    virtual void setStatus(int status);

    bool dirty() const;
    void setDirty(bool dirty);

private:
    std::map<std::string, std::string> m_fields;
    bool m_dirty = false;
};

class Entries : public std::enable_shared_from_this<Entries>
{
public:
    typedef std::shared_ptr<Entries> Ptr;
    typedef std::vector<Entry::Ptr> EntriesContainerType;

    Entry::Ptr get(size_t i);
    size_t size() const;
    void addEntry(Entry::Ptr entry);
    bool dirty() const;
    void setDirty(bool dirty);
    void removeEntry(size_t index);

private:
    EntriesContainerType m_entries;
    bool m_dirty = false;
};

class Condition : public std::enable_shared_from_this<Condition>
{
public:
    typedef std::shared_ptr<Condition> Ptr;

    enum Op
    {
        eq = 0,
        ne,
        gt,
        ge,
        lt,
        le
    };

    virtual ~Condition() {}

    virtual void EQ(const std::string& key, const std::string& value);
    virtual void NE(const std::string& key, const std::string& value);

    virtual void GT(const std::string& key, const std::string& value);
    virtual void GE(const std::string& key, const std::string& value);

    virtual void LT(const std::string& key, const std::string& value);
    virtual void LE(const std::string& key, const std::string& value);

    virtual void limit(size_t count);
    virtual void limit(size_t offset, size_t count);

    virtual std::unordered_map<std::string, std::pair<Op, std::string>>* getConditions();

private:
    std::unordered_map<std::string, std::pair<Op, std::string>> m_conditions;
    size_t m_offset = 0;
    size_t m_count = 0;
};

using Parallel = std::true_type;
using Serial = std::false_type;

template <typename Mode = Serial>
class Table;

struct Change
{
    enum Kind : int
    {
        /// Change::key contains the insert key,
        /// Change::value the insert entry.
        Insert,
        Update,
        Remove,
        Select
    };
    std::shared_ptr<Table<Serial>> table;
    Kind kind;  ///< The kind of the change.
    std::string key;
    struct Record
    {
        size_t index;
        std::string key;
        std::string oldValue;
        Record(
            size_t _index, std::string _key = std::string(), std::string _oldValue = std::string())
          : index(_index), key(_key), oldValue(_oldValue)
        {}
    };
    std::vector<Record> value;
    Change(std::shared_ptr<Table<Serial>> _table, Kind _kind, std::string const& _key,
        std::vector<Record>& _value)
      : table(_table), kind(_kind), key(_key), value(std::move(_value))
    {}
};

class TableBase : public std::enable_shared_from_this<TableBase>
{
public:
    typedef std::shared_ptr<TableBase>> Ptr;

    virtual Entries::Ptr select(const std::string& key, Condition::Ptr condition) = 0;
    virtual int update(const std::string& key, Entry::Ptr entry, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) = 0;
    virtual int insert(const std::string& key, Entry::Ptr entry,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) = 0;
    virtual int remove(const std::string& key, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) = 0;
    virtual bool checkAuthority(Address const& _origin) const = 0;
    virtual h256 hash() = 0;
    virtual void clear() = 0;
};


// Construction of transaction execution
template <typename Mode = Serial>
class Table : public TableBase
{
public:
    typedef std::shared_ptr<Table<Mode>> Ptr;
    typedef typename std::conditional<Mode::value,
        tbb::concurrent_unordered_map<std::string, Entries::Ptr>,
        std::unordered_map<std::string, Entries::Ptr>>::type DataType;

    virtual ~Table() = default;

    virtual Entry::Ptr newEntry() { return std::make_shared<Entry>(); }
    virtual Condition::Ptr newCondition() { return std::make_shared<Condition>(); }

    void setRecorder(
        std::function<void(Ptr, Change::Kind, std::string const&, std::vector<Change::Record>&)>
            _recorder)
    {
        m_recorder = _recorder;
    }

    DataType* data() { return nullptr; }

protected:
    typename std::enable_if<!Mode::value, std::function<void(Ptr, Change::Kind, std::string const&,
                                              std::vector<Change::Record>&)>>::type m_recorder;
};
}  // namespace storage
}  // namespace dev