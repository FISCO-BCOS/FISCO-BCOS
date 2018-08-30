/*
 * Storage.cpp
 *
 *  Created on: 2018年4月27日
 *      Author: mo nan
 */

#include "StorageException.h"

#include <boost/lexical_cast.hpp>
#include <map>
#include "StateDB.h"

using namespace dev::storage;

Entry::Entry() {
  // status为必选字段
  _fields.insert(std::make_pair("status", "0"));
}

std::string Entry::getField(const std::string &key) const {
  auto it = _fields.find(key);

  if (it != _fields.end()) {
    return it->second;
  } else {
    throw StorageException(-1, "未找到字段: " + key);
  }

  return "";
}

void Entry::setField(const std::string &key, std::string value) {
  auto it = _fields.find(key);

  if (it != _fields.end()) {
    it->second = value;
  } else {
    _fields.insert(std::make_pair(key, value));
  }

  _dirty = true;
}

std::map<std::string, std::string> *Entry::fields() { return &_fields; }

uint32_t Entry::getStatus() {
  auto it = _fields.find("status");
  if (it == _fields.end()) {
    return 0;
  } else {
    return boost::lexical_cast<uint32_t>(it->second);
  }
}

void Entry::setStatus(int status) {
  auto it = _fields.find("status");
  if (it == _fields.end()) {
    _fields.insert(
        std::make_pair("status", boost::lexical_cast<std::string>(status)));
  } else {
    it->second = boost::lexical_cast<std::string>(status);
  }

  _dirty = true;
}

bool Entry::dirty() const { return _dirty; }

void Entry::setDirty(bool dirty) { _dirty = dirty; }

Entry::Ptr Entries::get(size_t i) {
  if (_entries.size() <= i) {
    throw StorageException(
        -1, "Entries不存在该元素: " + boost::lexical_cast<std::string>(i));

    return Entry::Ptr();
  }

  return _entries[i];
}

size_t Entries::size() const { return _entries.size(); }

void Entries::addEntry(Entry::Ptr entry) {
  _entries.push_back(entry);
  _dirty = true;
}

bool Entries::dirty() const { return _dirty; }

void Entries::setDirty(bool dirty) { _dirty = dirty; }

void Condition::EQ(const std::string &key, const std::string &value) {
  _conditions.insert(std::make_pair(key, std::make_pair(Op::eq, value)));
}

void Condition::NE(const std::string &key, const std::string &value) {
  _conditions.insert(std::make_pair(key, std::make_pair(Op::ne, value)));
}

void Condition::GT(const std::string &key, const std::string &value) {
  _conditions.insert(std::make_pair(key, std::make_pair(Op::gt, value)));
}

void Condition::GE(const std::string &key, const std::string &value) {
  _conditions.insert(std::make_pair(key, std::make_pair(Op::ge, value)));
}

void Condition::LT(const std::string &key, const std::string &value) {
  _conditions.insert(std::make_pair(key, std::make_pair(Op::lt, value)));
}

void Condition::LE(const std::string &key, const std::string &value) {
  _conditions.insert(std::make_pair(key, std::make_pair(Op::le, value)));
}

void Condition::limit(size_t count) { limit(0, count); }

void Condition::limit(size_t offset, size_t count) {
  _offset = offset;
  _count = count;
}

std::map<std::string, std::pair<Condition::Op, std::string> >
    *Condition::getConditions() {
  return &_conditions;
}

Entry::Ptr StateDB::newEntry() { return std::make_shared<Entry>(); }
Condition::Ptr StateDB::newCondition() { return std::make_shared<Condition>(); }
