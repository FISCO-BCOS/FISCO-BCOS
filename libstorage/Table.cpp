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
/** @file Table.cpp
 *  @author ancelmo
 *  @date 20180921
 */
/*
 * Table.cpp
 *
 *  Created on: 2018.4.27
 *      Author: mo nan
 */

#include "StorageException.h"

#include "Common.h"
#include "Table.h"
#include <libdevcore/easylog.h>
#include <boost/lexical_cast.hpp>

using namespace dev::storage;

Entry::Entry()
{
    m_fields.insert(std::make_pair(ID_FIELD, "0"));
    m_fields.insert(std::make_pair(STATUS, "0"));
}

uint32_t Entry::getID() const
{
    // dev::ReadGuard l(x_fields);
    auto it = m_fields.find(ID_FIELD);
    if (it == m_fields.end())
    {
        return 0;
    }
    else
    {
        return boost::lexical_cast<uint32_t>(it->second);
    }
}

void Entry::setID(uint32_t id)
{
    auto it = m_fields.find(ID_FIELD);
    if (it == m_fields.end())
    {
        m_fields.insert(std::make_pair(ID_FIELD, boost::lexical_cast<std::string>(id)));
    }
    else
    {
        it->second = boost::lexical_cast<std::string>(id);
    }

    m_dirty = true;
}

std::string Entry::getField(const std::string& key) const
{
    auto it = m_fields.find(key);

    if (it != m_fields.end())
    {
        return it->second;
    }
    STORAGE_LOG(ERROR) << LOG_BADGE("Entry") << LOG_DESC("can't find key") << LOG_KV("key", key);

    return "";
}

void Entry::setField(const std::string& key, const std::string& value)
{
    // dev::WriteGuard l(x_fields);
    auto it = m_fields.find(key);

    if (it != m_fields.end())
    {
        it->second = value;
    }
    else
    {
        m_fields.insert(std::make_pair(key, value));
    }

    m_dirty = true;
}

size_t Entry::getTempIndex() const
{
    return m_tempIndex;
}

void Entry::setTempIndex(size_t index)
{
    m_tempIndex = index;
}

const std::map<std::string, std::string>* Entry::fields() const
{
    return &m_fields;
}

uint32_t Entry::getStatus() const
{
    // dev::ReadGuard l(x_fields);
    auto it = m_fields.find(STATUS);
    if (it == m_fields.end())
    {
        return 0;
    }
    else
    {
        return boost::lexical_cast<uint32_t>(it->second);
    }
}

void Entry::setStatus(int status)
{
    // dev::WriteGuard l(x_fields);
    auto it = m_fields.find(STATUS);
    if (it == m_fields.end())
    {
        m_fields.insert(std::make_pair(STATUS, boost::lexical_cast<std::string>(status)));
    }
    else
    {
        it->second = boost::lexical_cast<std::string>(status);
    }

    m_dirty = true;
}

uint32_t Entry::num() const {
	auto it = m_fields.find(NUM_FIELD);
	if(it == m_fields.end()) {
		return 0;
	}
	else {
		return boost::lexical_cast<uint32_t>(it->second);
	}
}

void Entry::setNum(uint32_t num) {
	auto it = m_fields.find(NUM_FIELD);
	if(it == m_fields.end()) {
		m_fields.insert(std::make_pair(NUM_FIELD, boost::lexical_cast<std::string>(num)));
	}
	else {
		it->second = boost::lexical_cast<std::string>(num);
	}

	m_dirty = true;
}

bool Entry::dirty() const
{
    return m_dirty;
}

void Entry::setDirty(bool dirty)
{
    m_dirty = dirty;
}

void Entry::copyFrom(Entry::Ptr entry) {
	m_dirty = entry->m_dirty;
	m_fields = entry->m_fields;
	m_tempIndex = entry->m_tempIndex;
}

size_t Entries::size() const
{
    return m_entries.size();
}

Entry::ConstPtr Entries::get(size_t i) const {
	if (m_entries.size() <= i)
	{
		throw StorageException(-1, "Entries no exists: " + boost::lexical_cast<std::string>(i));

		return Entry::Ptr();
	}

	return m_entries[i];
}

Entry::Ptr Entries::get(size_t i)
{
    if (m_entries.size() <= i)
    {
        throw StorageException(-1, "Entries no exists: " + boost::lexical_cast<std::string>(i));

        return Entry::Ptr();
    }

    return m_entries[i];
}

void Entries::addEntry(Entry::Ptr entry)
{
    m_entries.push_back(entry);
    m_dirty = true;
}

void Entries::removeEntry(size_t index)
{
    m_entries.erase(m_entries.begin() + index);
}

bool Entries::dirty() const
{
    return m_dirty;
}

void Entries::setDirty(bool dirty)
{
    m_dirty = dirty;
}

std::vector<Entry::Ptr>* Entries::entries() {
	return &m_entries;
}

size_t ConcurrentEntries::size() const
{
    return m_entries.size();
}

Entry::Ptr ConcurrentEntries::get(size_t i)
{
    if (m_entries.size() <= i)
    {
        throw StorageException(-1, "Entries no exists: " + boost::lexical_cast<std::string>(i));

        return Entry::Ptr();
    }

    // at() is more safe than operator[](), at() will check whether there is a memory overread
    return m_entries.at(i);
}

typename ConcurrentEntries::EntriesIter ConcurrentEntries::addEntry(Entry::Ptr entry)
{
    auto iter = m_entries.push_back(entry);
    m_dirty = true;
    return iter;
}

bool ConcurrentEntries::dirty() const
{
    return m_dirty;
}

void ConcurrentEntries::setDirty(bool dirty)
{
    m_dirty = dirty;
}

void Condition::EQ(const std::string& key, const std::string& value)
{
    //m_conditions.insert(std::make_pair(key, std::make_pair(Op::eq, value)));
    auto it = m_conditions.find(key);
    if(it != m_conditions.end()) {
    	it->second.left.first = true;
    	it->second.left.second = value;
    	it->second.right.first = true;
    	it->second.right.second = value;
    }
    else {
    	m_conditions.insert(std::make_pair(key, Range(std::make_pair(true, value), std::make_pair(true, value))));
    }
}

void Condition::NE(const std::string& key, const std::string& value)
{
	//not equal contains two area
    //m_conditions.insert(std::make_pair(key, std::make_pair(Op::ne, value)));
	auto it = m_conditions.find(key);
	if(it != m_conditions.end()) {
		it->second.right.first = false;
		it->second.right.second = value;
		it->second.left.first = false;
		it->second.left.second = value;
	}
	else {
		m_conditions.insert(std::make_pair(key, Range(std::make_pair(false, value), std::make_pair(false, value))));
	}
}

void Condition::GT(const std::string& key, const std::string& value)
{
    //m_conditions.insert(std::make_pair(key, std::make_pair(Op::gt, value)));
	auto it = m_conditions.find(key);
	if(it != m_conditions.end()) {
		it->second.left.first = false;
		it->second.left.second = value;
	}
	else {
		m_conditions.insert(std::make_pair(key, Range(std::make_pair(false, value), std::make_pair(false, UNLIMITED))));
	}
}

void Condition::GE(const std::string& key, const std::string& value)
{
    //m_conditions.insert(std::make_pair(key, std::make_pair(Op::ge, value)));
	auto it = m_conditions.find(key);
	if(it != m_conditions.end()) {
		it->second.left.first = true;
		it->second.left.second = value;
	}
	else {
		m_conditions.insert(std::make_pair(key, Range(std::make_pair(true, value), std::make_pair(false, UNLIMITED))));
	}
}

void Condition::LT(const std::string& key, const std::string& value)
{
    //m_conditions.insert(std::make_pair(key, std::make_pair(Op::lt, value)));
	auto it = m_conditions.find(key);
	if(it != m_conditions.end()) {
		it->second.right.first = false;
		it->second.right.second = value;
	}
	else {
		m_conditions.insert(std::make_pair(key, Range(std::make_pair(false, UNLIMITED), std::make_pair(false, value))));
	}
}

void Condition::LE(const std::string& key, const std::string& value)
{
    //m_conditions.insert(std::make_pair(key, std::make_pair(Op::le, value)));
	auto it = m_conditions.find(key);
	if(it != m_conditions.end()) {
		it->second.right.first = true;
		it->second.right.second = value;
	}
	else {
		m_conditions.insert(std::make_pair(key, Range(std::make_pair(false, UNLIMITED), std::make_pair(true, value))));
	}
}

void Condition::limit(size_t count)
{
    limit(0, count);
}

void Condition::limit(size_t offset, size_t count)
{
    m_offset = offset;
    m_count = count;
}

std::map<std::string, Condition::Range>* Condition::getConditions()
{
    return &m_conditions;
}

bool Condition::process(Entry::Ptr entry)
{
    try
    {
    	if(entry->getStatus() == Entry::Status::DELETED) {
    		return false;
    	}

    	auto fields = entry->fields();

    	for(auto it: *fields) {
    		auto condIt = m_conditions.find(it.first);
    		if(condIt != m_conditions.end()) {
    			if(condIt->second.left.second == condIt->second.right.second && condIt->second.left.first && condIt->second.right.first) {
    				if(condIt->second.left.second == it.second) {
    					// point hited
    					continue;
    				}
    				else {
    					// point missed
    					return false;
    				}
    			}

    			if(condIt->second.left.second != UNLIMITED) {
					auto lhs = boost::lexical_cast<int64_t>(condIt->second.left.second);
					auto rhs = (int64_t)0;
					if(!it.second.empty()) {
						rhs = boost::lexical_cast<int64_t>(it.second);
					}

					if(condIt->second.left.first) {
						if(!(lhs <= rhs)) {
							return false;
						}
					}
					else {
						if(!(lhs < rhs)) {
							return false;
						}
					}
				}

				if(condIt->second.right.second != UNLIMITED) {
					auto lhs = boost::lexical_cast<int64_t>(condIt->second.right.second);
					auto rhs = (int64_t)0;
					if(!it.second.empty()) {
						rhs = boost::lexical_cast<int64_t>(it.second);
					}

					if(condIt->second.right.first) {
						if(!(lhs >= rhs)) {
							return false;
						}
					}
					else {
						if(!(lhs > rhs)) {
							return false;
						}
					}
				}
    		}
    	}

#if 0
    	for(auto it : m_conditions) {
    		auto fieldIt = fields->find(it.first);

    		if(fieldIt != fields->end()) {
    			auto value = fieldIt->second;

    			if(it.second.left.second == it.second.right.second) {
    				if(it.second.left.first && it.second.right.first) {
    					if(it.second.left.second == value) {
    						continue;
    					}
    					else {
    						return false;
    					}
    				}
    			}

    			if(it.second.left.second != UNLIMITED) {
    				auto lhs = boost::lexical_cast<int64_t>(it.second.left.second);
    				auto rhs = (int64_t)0;
    				if(!value.empty()) {
    					rhs = boost::lexical_cast<int64_t>(value);
    				}

    				if(it.second.left.first) {
    					if(!(lhs <= rhs)) {
    						return false;
    					}
    				}
    				else {
    					if(!(lhs < rhs)) {
							return false;
						}
    				}
    			}

    			if(it.second.right.second != UNLIMITED) {
    				auto lhs = boost::lexical_cast<int64_t>(it.second.right.second);
					auto rhs = (int64_t)0;
					if(!value.empty()) {
						rhs = boost::lexical_cast<int64_t>(value);
					}

    				if(it.second.right.first) {
    					if(!(lhs >= rhs)) {
							return false;
						}
    				}
    				else {
    					if(!(lhs > rhs)) {
							return false;
						}
    				}
    			}
    		}
    	}
#endif
    }
    catch (std::exception& e)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("Condition") << LOG_DESC("process error")
                           << LOG_KV("msg", boost::diagnostic_information(e));
        return false;
    }

    return true;
}

bool Condition::graterThan(Condition::Ptr condition) {
	(void)condition;
#if 0
	try {
		for(auto it: *(condition->getConditions())) {
			auto condIt = m_conditions.find(it.first);
			if(condIt != m_conditions.end()) {
				//equal condition
				if(condIt->second.left.second == condIt->second.right.second) {
					if(it.second.left.second != it.second.right.second) {
						// eq vs range
						return false;
					}

					// value equal?
					if(!(condIt->second.left.second == it.second.left.second)) {
						return false;
					}

					// close range or open range?
					if(!condIt->second.left.first && it.second.left.first) {
						return false;
					}

					if(!condIt->second.right.first && it.second.right.first) {
						return false;
					}
				}

				if(condIt->second.left.second != UNLIMITED) {
					if(it.second.left.second == UNLIMITED) {
						return false;
					}

					auto lhs = boost::lexical_cast<int64_t>(condIt->second.left.second);
					auto rhs = boost::lexical_cast<int64_t>(it.second.left.second);
					if(condIt->second.left.first && !(lhs <= rhs)) {
						return false;
					}

					if(!condIt->second.left.first) {
						if(it.second.left.first) {
							if(!(lhs < rhs)) {
								return false;
							}
						}
						else {
							if(!(lhs <= rhs)) {
								return false;
							}
						}
					}
				}

				if(condIt->second.right.second != UNLIMITED) {
					if(it.second.right.second == UNLIMITED) {
						return false;
					}

					auto lhs = boost::lexical_cast<int64_t>(condIt->second.right.second);
					auto rhs = boost::lexical_cast<int64_t>(it.second.right.second);
					if(condIt->second.right.first && !(lhs >= rhs)) {
						return false;
					}

					if(!condIt->second.right.first) {
						if(it.second.right.first) {
							if(!(lhs > rhs)) {
								return false;
							}
						}
						else {
							if(!(lhs >= rhs)) {
								return false;
							}
						}
					}
				}
			}
		}
	}
	catch(std::exception &e) {
		STORAGE_LOG(WARNING) << "Error while graterThan condition: " << boost::diagnostic_information(e);
		return false;
	}
#endif

	return true;
}

bool Condition::related(Condition::Ptr condition) {
	(void)condition;
#if 0
	try {
		for(auto condIt: m_conditions) {
			auto it = condition->getConditions()->find(condIt.first);
			if(it == condition->getConditions()->end()) {
				return true;
			}
			else {
				if(it->second.left.first && condIt.second.left.first && it->second.left.second == condIt.second.left.second) {
					return true;
				}

				if(it->second.right.first && condIt.second.right.first && it->second.right.second == condIt.second.right.second) {
					return true;
				}

				if(condIt.second.left.second == UNLIMITED || it->second.left.second == UNLIMITED) {
					return true;
				}

				if(condIt.second.right.second == UNLIMITED || it->second.right.second == UNLIMITED) {
					return true;
				}

				auto leftLHS = boost::lexical_cast<int64_t>(condIt.second.left.second);
				auto leftRHS = boost::lexical_cast<int64_t>(it->second.left.second);
				auto rightLHS = boost::lexical_cast<int64_t>(condIt.second.right.second);
				auto rightRHS = boost::lexical_cast<int64_t>(it->second.right.second);

				if(leftLHS < rightLHS && leftRHS < rightLHS) {
					return false;
				}

				if(leftLHS > rightRHS && leftRHS > rightRHS) {
					return false;
				}

				return true;
			}
		}
	}
	catch(std::exception &e) {
		STORAGE_LOG(WARNING) << "Error while related condition: " << boost::diagnostic_information(e);
		return false;
	}
#endif

	return false;
}
