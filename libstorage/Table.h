#pragma once

#include <libdevcore/FixedHash.h>
#include <map>
#include <memory>
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
};

class Entry : public std::enable_shared_from_this<Entry>
{
public:
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

    virtual ~Entries() {}

    virtual Entry::Ptr get(size_t i);

    virtual size_t size() const;

    virtual void addEntry(Entry::Ptr entry);
    virtual void removeEntry(size_t index);

    bool dirty() const;
    void setDirty(bool dirty);

private:
    std::vector<Entry::Ptr> m_entries;
    bool m_dirty;
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

    virtual std::map<std::string, std::pair<Op, std::string> >* getConditions();

private:
    std::map<std::string, std::pair<Op, std::string> > m_conditions;
    size_t m_offset = 0;
    size_t m_count = 0;
};

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
        Record(
            size_t _index, std::string _key = std::string(), std::string _oldValue = std::string())
          : index(_index), key(_key), oldValue(_oldValue)
        {}
    };
    std::vector<Record> value;
    Change(std::shared_ptr<Table> _table, Kind _kind, std::string const& _key,
        std::vector<Record>& _value)
      : table(_table), kind(_kind), key(_key), value(std::move(_value))
    {}
};

// Construction of transaction execution
class Table : public std::enable_shared_from_this<Table>
{
public:
    typedef std::shared_ptr<Table> Ptr;

    virtual ~Table() {}

    virtual Entries::Ptr select(const std::string& key, Condition::Ptr condition) = 0;
    virtual size_t update(const std::string& key, Entry::Ptr entry, Condition::Ptr condition) = 0;
    virtual size_t insert(const std::string& key, Entry::Ptr entry) = 0;
    virtual size_t remove(const std::string& key, Condition::Ptr condition) = 0;

    virtual Entry::Ptr newEntry();
    virtual Condition::Ptr newCondition();
    virtual void setRecorder(
        std::function<void(Ptr, Change::Kind, std::string const&, std::vector<Change::Record>&)>
            _recorder)
    {
        m_recorder = _recorder;
    }
    virtual h256 hash() = 0;
    virtual void clear() = 0;
    virtual std::map<std::string, Entries::Ptr>* data() { return NULL; }

protected:
    std::function<void(Ptr, Change::Kind, std::string const&, std::vector<Change::Record>&)>
        m_recorder;
};

// Block execution time construction
class StateDBFactory : public std::enable_shared_from_this<StateDBFactory>
{
public:
    typedef std::shared_ptr<StateDBFactory> Ptr;

    virtual ~StateDBFactory() {}

    virtual Table::Ptr openTable(h256 blockHash, int64_t num, const std::string& table) = 0;
    virtual Table::Ptr createTable(h256 blockHash, int64_t num, const std::string& tableName,
        const std::string& keyField, const std::string& valueField) = 0;
};

}  // namespace storage

}  // namespace dev
