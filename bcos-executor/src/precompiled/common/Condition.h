#pragma once

#include <bcos-framework/protocol/Exceptions.h>
#include <bcos-framework/storage/Common.h>
#include <map>
#include <memory>

namespace bcos::precompiled
{
class Condition
{
public:
    Condition() = default;
    ~Condition() = default;


    static inline constexpr storage::Condition::Comparator covertComparator(uint8_t cmp)
    {
        if (cmp > (uint8_t)storage::Condition::Comparator::CONTAINS)
        {
            BOOST_THROW_EXCEPTION(
                protocol::PrecompiledError(std::to_string(cmp) + " ConditionOP not exist!"));
        }
        return static_cast<storage::Condition::Comparator>(cmp);
    }

    void addOp(uint8_t cmp, uint32_t idx, const std::string& value)
    {
        if (!m_conditions.contains(idx))
        {
            m_conditions[idx] = std::make_shared<storage::Condition>();
        }
        m_conditions[idx]->m_conditions.emplace_back(covertComparator(cmp), value);
    }

    inline void insert(uint32_t key, std::shared_ptr<storage::Condition> cond)
    {
        m_conditions[key] = std::move(cond);
    }

    void limitKey(size_t start, size_t count)
    {
        if (!m_conditions.contains(0) || m_conditions[0] == nullptr)
        {
            m_conditions[0] = std::make_shared<storage::Condition>();
        }
        m_conditions.at(0)->m_limit = {start, count};
    }

    void limit(size_t start, size_t count) { m_limit = std::pair<size_t, size_t>(start, count); }
    std::pair<size_t, size_t> getLimit() const { return {m_limit.first, m_limit.second}; }
    std::pair<size_t, size_t> getKeyLimit() const
    {
        if (!m_conditions.contains(0))
        {
            return {0, 0};
        }
        return m_conditions.at(0)->m_limit;
    }
    inline bool contains(uint32_t key) const noexcept { return m_conditions.contains(key); }
    inline size_t size() const noexcept { return m_conditions.size(); }
    inline std::shared_ptr<storage::Condition> at(uint32_t key)
    {
        if (!m_conditions.contains(key))
        {
            return nullptr;
        }
        return m_conditions.at(key);
    }

    bool isValid(const std::vector<std::string>& values) const
    {
        for (const auto& cond : m_conditions)
        {
            if (cond.first > values.size())
            {
                PRECOMPILED_LOG(DEBUG)
                    << LOG_DESC("The field index is greater than the size of fields");
                BOOST_THROW_EXCEPTION(bcos::protocol::PrecompiledError(
                    "The field index is greater than the size of fields"));
            }
            if (cond.first == 0)
            {
                continue;
            }
            if (!cond.second->isValid(values[cond.first - 1]))
                return false;
        }
        return true;
    }

private:
    std::map<uint32_t, std::shared_ptr<storage::Condition>> m_conditions;
    std::pair<size_t, size_t> m_limit;
};
}  // namespace bcos::precompiled