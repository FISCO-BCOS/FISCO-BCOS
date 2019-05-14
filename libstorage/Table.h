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
#include <tbb/tbb_allocator.h>
#include <atomic>
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
    std::vector<std::string> indices;
};

struct AccessOptions : public std::enable_shared_from_this<AccessOptions>
{
    typedef std::shared_ptr<AccessOptions> Ptr;
    AccessOptions() = default;
    AccessOptions(Address _origin, bool _check = true) : origin(_origin), check(_check) {}
    Address origin;
    bool check = true;
};

class Entry : public std::enable_shared_from_this<Entry>
{
public:
    typedef std::shared_ptr<Entry> Ptr;
    typedef std::shared_ptr<const Entry> ConstPtr;

    enum Status
    {
        NORMAL = 0,
        DELETED
    };

    Entry();
    virtual ~Entry();

    virtual uint32_t getID() const;
    virtual void setID(uint32_t id);
    virtual void setID(const std::string& id);

    virtual std::string getField(const std::string& key) const;
    virtual void setField(const std::string& key, const std::string& value);

    virtual size_t getTempIndex() const;
    virtual void setTempIndex(size_t index);

    virtual const std::map<std::string, std::string>* fields() const;

    virtual int getStatus() const;
    virtual void setStatus(int status);
    virtual void setStatus(const std::string& status);

    virtual uint32_t num() const;
    virtual void setNum(uint32_t num);

    virtual bool dirty() const;
    virtual void setDirty(bool dirty);

    // set the force flag will force insert the entry without query
    virtual bool force() const;
    virtual void setForce(bool force);

    virtual bool deleted() const;
    virtual void setDeleted(bool deleted);

    virtual ssize_t capacity() const;

    virtual void copyFrom(Entry::Ptr entry);

private:
    struct EntryData
    {
        typedef std::shared_ptr<EntryData> Ptr;

        EntryData(std::shared_ptr<size_t> refCount,
            std::shared_ptr<std::map<std::string, std::string>> fields)
          : m_refCount(refCount), m_fields(fields){};

        std::shared_ptr<size_t> m_refCount;
        std::shared_ptr<std::map<std::string, std::string>> m_fields;
    };

    void checkRef();

    uint32_t m_ID = 0;
    int m_status = 0;
    size_t m_tempIndex = 0;
    uint32_t m_num = 0;
    bool m_dirty = false;
    bool m_force = false;
    bool m_deleted = false;
    ssize_t m_capacity = 0;

    EntryData::Ptr m_data;
};

class EntryLess
{
public:
    EntryLess(TableInfo::Ptr tableInfo) : m_tableInfo(tableInfo){};
    virtual ~EntryLess(){};

    virtual bool operator()(const Entry::Ptr& lhs, const Entry::Ptr& rhs) const;

private:
    TableInfo::Ptr m_tableInfo;
};


class Entries : public std::enable_shared_from_this<Entries>
{
public:
    typedef tbb::concurrent_vector<Entry::Ptr> Vector;

    typedef std::shared_ptr<Entries> Ptr;
    typedef std::shared_ptr<const Entries> ConstPtr;
    virtual ~Entries(){};

    virtual Vector::iterator begin();
    virtual Vector::iterator end();

    virtual Vector::reference operator[](Vector::size_type index);

    virtual Entry::ConstPtr get(size_t i) const;
    virtual Entry::Ptr get(size_t i);
    virtual size_t size() const;
    virtual void resize(size_t n);
    virtual size_t addEntry(Entry::Ptr entry);
    virtual bool dirty() const;
    virtual void setDirty(bool dirty);
    virtual void removeEntry(size_t index);

    virtual void shallowFrom(Entries::Ptr entries);

private:
    Vector m_entries;
    bool m_dirty = false;
};

class ConcurrentEntries : public std::enable_shared_from_this<ConcurrentEntries>
{
public:
    using Ptr = std::shared_ptr<ConcurrentEntries>;

    /*
    tbb::concurrent_vector class DOES NOT support a thread traverse across a tbb::concurrent_vector
    instance safely in parallel to growth operations of other threads, nether using simple C-style
    for-loop with "i < v.size()" or "iter != v.end()" nor C++-11-style for-loop "for(auto i: v)",
    please visit:
    https://software.intel.com/en-us/blogs/2009/04/09/delusion-of-tbbconcurrent_vectors-size-or-3-ways-to-traverse-in-parallel-correctly
    to get an insight about this problem and the solution.
    */
    using EntriesContainer =
        tbb::concurrent_vector<typename Entry::Ptr, tbb::zero_allocator<typename Entry::Ptr>>;
    using EntriesIter = typename EntriesContainer::iterator;

    typename Entry::Ptr get(size_t i);
    size_t size() const;
    EntriesIter addEntry(Entry::Ptr entry);
    bool dirty() const;
    void setDirty(bool dirty);

    /*
    To support grow in parallel, tbb::concurrent_vector DOES NOT represent a contiguous array/memory
    region for storing its elements. In Reference Manual for developers, it says:

    Unlike a std::vector, a concurrent_vector never moves existing elements when it grows.
    The container allocates a series of contiguous arrays. ...

    So the iterator begin() method returns will never change.
    */
    EntriesIter begin() { return m_entries.begin(); }
    EntriesIter end() { return m_entries.end(); }

private:
    EntriesContainer m_entries;
    std::atomic_bool m_dirty;
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


    class Range
    {
    public:
        Range(std::pair<bool, std::string> _left, std::pair<bool, std::string> _right)
          : left(_left), right(_right){};

        // false is close range '<', true is open range '<='
        std::pair<bool, std::string> left;
        std::pair<bool, std::string> right;
    };

    virtual ~Condition() {}

    virtual void EQ(const std::string& key, const std::string& value);
    virtual void NE(const std::string& key, const std::string& value);

    virtual void GT(const std::string& key, const std::string& value);
    virtual void GE(const std::string& key, const std::string& value);

    virtual void LT(const std::string& key, const std::string& value);
    virtual void LE(const std::string& key, const std::string& value);

    virtual void limit(int64_t count);
    virtual void limit(int64_t offset, int64_t count);
    virtual std::map<std::string, Range>* getConditions();

    virtual bool process(Entry::Ptr entry);
    virtual bool graterThan(Condition::Ptr condition);
    virtual bool related(Condition::Ptr condition);

    virtual Entries::Ptr processEntries(Entries::Ptr entries);

    virtual std::string unlimitedField() { return UNLIMITED; }

    virtual int getOffset();
    virtual int getCount();


private:
    int64_t m_offset = -1;
    int64_t m_count = -1;
    std::map<std::string, Range> m_conditions;
    const std::string UNLIMITED = "_VALUE_UNLIMITED_";
};

using Parallel = std::true_type;
using Serial = std::false_type;

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
    std::shared_ptr<Table> table;
    Kind kind;  ///< The kind of the change.
    std::string key;
    struct Record
    {
        size_t index;
        std::string key;
        std::string oldValue;
        size_t id;
        Record(size_t _index, std::string _key = std::string(),
            std::string _oldValue = std::string(), size_t _id = 0)
          : index(_index), key(_key), oldValue(_oldValue), id(_id)
        {}
    };
    std::vector<Record> value;
    Change(std::shared_ptr<Table> _table, Kind _kind, std::string const& _key,
        std::vector<Record>& _value)
      : table(_table), kind(_kind), key(_key), value(std::move(_value))
    {}
};

class TableData
{
public:
    typedef std::shared_ptr<TableData> Ptr;

    TableData()
    {
        info = std::make_shared<TableInfo>();
        dirtyEntries = std::make_shared<Entries>();
        newEntries = std::make_shared<Entries>();
    }

    // for memorytable2
    TableInfo::Ptr info;
    Entries::Ptr dirtyEntries;
    Entries::Ptr newEntries;

    // only for memorytable, memoryTable2 don't need
    std::string tableName;
    std::map<std::string, Entries::Ptr> data;
};

// Construction of transaction execution
class Storage;
class Table : public std::enable_shared_from_this<Table>
{
public:
    typedef std::shared_ptr<Table> Ptr;

    virtual ~Table() = default;

    virtual Entry::Ptr newEntry() { return std::make_shared<Entry>(); }
    virtual Condition::Ptr newCondition() { return std::make_shared<Condition>(); }
    virtual Entries::ConstPtr select(const std::string& key, Condition::Ptr condition) = 0;
    virtual int update(const std::string& key, Entry::Ptr entry, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) = 0;
    virtual int insert(const std::string& key, Entry::Ptr entry,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>(), bool needSelect = true) = 0;
    virtual int remove(const std::string& key, Condition::Ptr condition,
        AccessOptions::Ptr options = std::make_shared<AccessOptions>()) = 0;
    virtual bool checkAuthority(Address const& _origin) const = 0;
    virtual h256 hash() = 0;
    virtual void clear() = 0;

    virtual dev::storage::TableData::Ptr dump() = 0;
    virtual void rollback(const Change& _change) = 0;
    virtual bool empty() = 0;
    virtual void setRecorder(
        std::function<void(Ptr, Change::Kind, std::string const&, std::vector<Change::Record>&)>
            _recorder)
    {
        m_recorder = _recorder;
    }

    virtual void setStateStorage(std::shared_ptr<Storage> amopDB) = 0;
    virtual void setBlockHash(h256 blockHash) = 0;
    virtual void setBlockNum(int blockNum) = 0;
    virtual void setTableInfo(TableInfo::Ptr tableInfo) { m_tableInfo = tableInfo; };
    virtual size_t cacheSize() { return 0; }

protected:
    std::function<void(Ptr, Change::Kind, std::string const&, std::vector<Change::Record>&)>
        m_recorder;
    TableInfo::Ptr m_tableInfo;
};

// Block execution time construction by TableFactoryFactory
class TableFactory : public std::enable_shared_from_this<TableFactory>
{
public:
    typedef std::shared_ptr<TableFactory> Ptr;

    virtual ~TableFactory() {}

    virtual Table::Ptr openTable(
        const std::string& table, bool authorityFlag = true, bool isPara = true) = 0;
    virtual Table::Ptr createTable(const std::string& tableName, const std::string& keyField,
        const std::string& valueField, bool authorityFlag, Address const& _origin = Address(),
        bool isPara = true) = 0;

    virtual h256 hash() = 0;
    virtual size_t savepoint() = 0;
    virtual void rollback(size_t _savepoint) = 0;
    virtual void commit() = 0;
    virtual void commitDB(h256 const& _blockHash, int64_t _blockNumber) = 0;
};

class TableFactoryFactory : public std::enable_shared_from_this<TableFactoryFactory>
{
public:
    typedef std::shared_ptr<TableFactoryFactory> Ptr;

    virtual ~TableFactoryFactory(){};

    virtual TableFactory::Ptr newTableFactory(dev::h256 hash, int64_t number) = 0;
};

}  // namespace storage
}  // namespace dev
