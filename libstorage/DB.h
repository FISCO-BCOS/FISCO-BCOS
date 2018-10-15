#pragma once

#include <libdevcore/FixedHash.h>
#include <map>
#include <memory>
#include <vector>
#include "Common.h"

namespace dev {

namespace storage {

struct TableInfo : public std::enable_shared_from_this<TableInfo> {
  typedef std::shared_ptr<TableInfo> Ptr;

  std::string name;
  std::string key;
  std::vector<std::string> fields;
};

class Entry : public std::enable_shared_from_this<Entry> {
 public:
  typedef std::shared_ptr<Entry> Ptr;

  enum Status { NORMAL = 0, DELETED };

  Entry();
  virtual ~Entry() {}

  virtual std::string getField(const std::string &key) const;
  virtual void setField(const std::string &key, std::string value);
  virtual std::map<std::string, std::string> *fields();

  virtual uint32_t getStatus();
  virtual void setStatus(int status);

  bool dirty() const;
  void setDirty(bool dirty);

 private:
  std::map<std::string, std::string> _fields;
  bool _dirty = false;
};

class Entries : public std::enable_shared_from_this<Entries> {
 public:
  typedef std::shared_ptr<Entries> Ptr;

  virtual ~Entries() {}

  virtual Entry::Ptr get(size_t i);

  virtual size_t size() const;

  virtual void addEntry(Entry::Ptr entry);

  bool dirty() const;
  void setDirty(bool dirty);

 private:
  std::vector<Entry::Ptr> _entries;
  bool _dirty;
};

class Condition : public std::enable_shared_from_this<Condition> {
 public:
  typedef std::shared_ptr<Condition> Ptr;

  enum Op { eq = 0, ne, gt, ge, lt, le };

  virtual ~Condition() {}

  virtual void EQ(const std::string &key, const std::string &value);
  virtual void NE(const std::string &key, const std::string &value);

  virtual void GT(const std::string &key, const std::string &value);
  virtual void GE(const std::string &key, const std::string &value);

  virtual void LT(const std::string &key, const std::string &value);
  virtual void LE(const std::string &key, const std::string &value);

  virtual void limit(size_t count);
  virtual void limit(size_t offset, size_t count);

  virtual std::map<std::string, std::pair<Op, std::string> > *getConditions();

 private:
  std::map<std::string, std::pair<Op, std::string> > _conditions;
  size_t _offset = 0;
  size_t _count = 0;
};

//交易执行时构建
class DB : public std::enable_shared_from_this<DB> {
 public:
  typedef std::shared_ptr<DB> Ptr;

  virtual ~DB() {}

  virtual Entries::Ptr select(const std::string &key,
                              Condition::Ptr condition) = 0;
  virtual size_t update(const std::string &key, Entry::Ptr entry,
                        Condition::Ptr condition) = 0;
  virtual size_t insert(const std::string &key, Entry::Ptr entry) = 0;
  virtual size_t remove(const std::string &key, Condition::Ptr condition) = 0;

  virtual Entry::Ptr newEntry();
  virtual Condition::Ptr newCondition();

  virtual h256 hash() = 0;
  virtual void clear() = 0;
  virtual std::map<std::string, Entries::Ptr> *data() { return NULL; }
};

//块执行时构建
class StateDBFactory : public std::enable_shared_from_this<StateDBFactory> {
 public:
  typedef std::shared_ptr<StateDBFactory> Ptr;

  virtual ~StateDBFactory() {}

  virtual DB::Ptr openTable(h256 blockHash, int num,
                                 const std::string &table) = 0;
  virtual DB::Ptr createTable(
      h256 blockHash, int num, const std::string &tableName,
      const std::string &keyField,
      const std::vector<std::string> &valueField) = 0;
};

}  // namespace storage

}  // namespace dev
