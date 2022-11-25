#pragma once

#include <bcos-framework/storage/Common.h>
#include <bcos-framework/protocol/Exceptions.h>
#include <map>
#include <memory>

namespace bcos::precompiled
{
class Condition
{
public:

    Condition() = default;
    ~Condition() = default;

    void EQ(uint32_t idx, const std::string& value) 
    { 
        addCondtion(idx, value, 
            [this](uint32_t idx, const std::string& value) {m_conditions[idx]->EQ(value);});
    }
    void NE(uint32_t idx, const std::string& value) 
    { 
        addCondtion(idx, value, 
            [this](uint32_t idx, const std::string& value) {m_conditions[idx]->NE(value);});
    }
    void GT(uint32_t idx, const std::string& value) 
    { 
        addCondtion(idx, value, 
            [this](uint32_t idx, const std::string& value) {m_conditions[idx]->GT(value);});
    }
    void GE(uint32_t idx, const std::string& value) 
    { 
        addCondtion(idx, value, 
            [this](uint32_t idx, const std::string& value) {m_conditions[idx]->GE(value);});
    }
    void LT(uint32_t idx, const std::string& value) 
    { 
        addCondtion(idx, value, 
            [this](uint32_t idx, const std::string& value) {m_conditions[idx]->LT(value);});
    }
    void LE(uint32_t idx, const std::string& value) 
    { 
        addCondtion(idx, value, 
            [this](uint32_t idx, const std::string& value) {m_conditions[idx]->LE(value);});
    }
    void STARTS_WITH(uint32_t idx, const std::string& value) 
    { 
        addCondtion(idx, value, 
            [this](uint32_t idx, const std::string& value) {m_conditions[idx]->STARTS_WITH(value);});
    }
    void ENDS_WITH(uint32_t idx, const std::string& value) 
    { 
        addCondtion(idx, value, 
            [this](uint32_t idx, const std::string& value) {m_conditions[idx]->ENDS_WITH(value);});
    }
    void CONTAINS(uint32_t idx, const std::string& value) 
    { 
        addCondtion(idx, value, 
            [this](uint32_t idx, const std::string& value) {m_conditions[idx]->CONTAINS(value);});
    }

    void limit(size_t start, size_t count) { m_limit = std::pair<size_t, size_t>(start, count); }
    std::pair<size_t, size_t> getLimit() const { return {m_limit.first, m_limit.second}; }

    bool isValid(const std::vector<std::string>& values) const
    {
        for(auto iter = m_conditions.begin(); iter != m_conditions.end(); ++iter)
        {
            if(iter->first > values.size())
            {
                PRECOMPILED_LOG(INFO) << LOG_DESC("The field index is greater than the size of fields");                                                       
                BOOST_THROW_EXCEPTION(bcos::protocol::PrecompiledError("The field index is greater than the size of fields")); 
            }
            if(!iter->second->isValid(values[iter->first - 1]))
                return false;
        }
        return true;
    }
private:
    template<typename Functor>
    void addCondtion(uint32_t idx, const std::string& value, Functor&& OP)
    {
        if (!m_conditions.contains(idx))
            m_conditions.insert(std::make_pair(idx, std::make_shared<storage::Condition>()));
        OP(idx, value);
    }
private:
    std::map<uint32_t, std::shared_ptr<storage::Condition>> m_conditions;
    std::pair<size_t, size_t> m_limit;
};
}  // namespace bcos::precompiled