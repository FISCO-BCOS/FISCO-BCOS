#include "bcos-framework/storage/Common.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <fmt/format.h>
#include <boost/test/unit_test.hpp>
#include <sstream>

using namespace bcos;

namespace bcos
{
namespace test
{

struct ConditionNative
{
    ConditionNative() = default;
    ~ConditionNative() = default;
    void EQ(const std::string& value) { m_conditions.emplace_back(Comparator::EQ, value); }
    void NE(const std::string& value) { m_conditions.emplace_back(Comparator::NE, value); }
    // string compare, "2" > "12"
    void GT(const std::string& value) { m_conditions.emplace_back(Comparator::GT, value); }
    void GE(const std::string& value) { m_conditions.emplace_back(Comparator::GE, value); }
    // string compare, "12" < "2"
    void LT(const std::string& value) { m_conditions.emplace_back(Comparator::LT, value); }
    void LE(const std::string& value) { m_conditions.emplace_back(Comparator::LE, value); }
    void startsWith(const std::string& value)
    {
        m_conditions.emplace_back(Comparator::STARTS_WITH, value);
    }
    void endsWith(const std::string& value)
    {
        m_conditions.emplace_back(Comparator::ENDS_WITH, value);
    }
    void contains(const std::string& value)
    {
        m_conditions.emplace_back(Comparator::CONTAINS, value);
    }

    bool isValid(const std::string_view& key) const
    {  // all conditions must be satisfied
        for (auto& cond : m_conditions)
        {  // conditions should few, so not parallel check for now
            switch (cond.cmp)
            {
            case Comparator::EQ:
                if (key != cond.value)
                {
                    return false;
                }
                break;
            case Comparator::NE:
                if (key == cond.value)
                {
                    return false;
                }
                break;
            case Comparator::GT:
                if (key <= cond.value)
                {
                    return false;
                }
                break;
            case Comparator::GE:
                if (key < cond.value)
                {
                    return false;
                }
                break;
            case Comparator::LT:
                if (key >= cond.value)
                {
                    return false;
                }
                break;
            case Comparator::LE:
                if (key > cond.value)
                {
                    return false;
                }
                break;
            case Comparator::STARTS_WITH:
                if (!key.starts_with(cond.value))
                {
                    return false;
                }
                break;
            case Comparator::ENDS_WITH:
                if (!key.ends_with(cond.value))
                {
                    return false;
                }
                break;
            case Comparator::CONTAINS:
                if (key.find(cond.value) == std::string::npos)
                {
                    return false;
                }
                break;
            default:
                // undefined Comparator
                break;
            }
        }
        return true;
    }

    enum class Comparator : uint8_t
    {
        GT = 0,
        GE = 1,
        LT = 2,
        LE = 3,
        EQ = 4,
        NE = 5,
        STARTS_WITH = 6,
        ENDS_WITH = 7,
        CONTAINS = 8
    };

    struct cond
    {
        cond(Comparator _cmp, const std::string& _value) : cmp(_cmp), value(_value) {}
        Comparator cmp;
        std::string value;
    };

    std::vector<cond> m_conditions;
};


class Condition
{
public:
    Condition() = default;
    ~Condition() = default;

    void EQ(const std::string& value)
    {
        m_condition1.EQ(value);
        m_condition2.EQ(value);
    }
    void NE(const std::string& value)
    {
        m_condition1.NE(value);
        m_condition2.NE(value);
    }
    void GT(const std::string& value)
    {
        m_condition1.GT(value);
        m_condition2.GT(value);
    }
    void GE(const std::string& value)
    {
        m_condition1.GE(value);
        m_condition2.GE(value);
    }
    void LT(const std::string& value)
    {
        m_condition1.LT(value);
        m_condition2.LT(value);
    }
    void LE(const std::string& value)
    {
        m_condition1.LE(value);
        m_condition2.LE(value);
    }
    void startsWith(const std::string& value)
    {
        m_condition1.startsWith(value);
        m_condition2.startsWith(value);
    }
    void endsWith(const std::string& value)
    {
        m_condition1.endsWith(value);
        m_condition2.endsWith(value);
    }
    void contains(const std::string& value)
    {
        m_condition1.contains(value);
        m_condition2.contains(value);
    }

    template <typename TypeCond>
    static std::vector<std::string> getKeys(
        const std::vector<std::string>& keys, const TypeCond& cond)
    {
        std::vector<std::string> ret;
        for (auto& key : keys)
        {
            if (cond.isValid(key))
            {
                ret.push_back(key);
            }
        }
        return ret;
    }

    bool isEquivalent(const std::vector<std::string>& data, bool emptyRes = false)
    {
        auto ret1 = getKeys(data, m_condition1);
        auto ret2 = getKeys(data, m_condition2);
        if (emptyRes)
        {
            return ret1 == ret2 && ret1.empty();
        }
        return ret1 == ret2;
    }

    bcos::storage::Condition getCondition() const { return m_condition1; }

private:
    bcos::storage::Condition m_condition1;
    ConditionNative m_condition2;
};

static std::string fillZero(int _num)
{
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(10) << std::right << _num;
    return stream.str();
}

static std::vector<std::string> generateData(int start, int end)
{
    std::vector<std::string> ret;
    for (int i = start; i < end; i++)
    {
        ret.push_back(fillZero(i));
    }
    return ret;
}

BOOST_FIXTURE_TEST_SUITE(TestCondition, TestPromptFixture)

BOOST_AUTO_TEST_CASE(testGE)
{
    auto testData = generateData(0, 20);

    // no conflict condition
    {
        Condition cond;
        cond.GE(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LT(fillZero(10));
        cond.GE(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LE(fillZero(10));
        cond.GE(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LE(fillZero(10));
        cond.LT(fillZero(10));
        cond.GE(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    // update condition
    {
        Condition cond;
        cond.GE(fillZero(5));
        cond.GE(fillZero(6));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LT(fillZero(10));
        cond.GE(fillZero(5));
        cond.GE(fillZero(6));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LE(fillZero(10));
        cond.GE(fillZero(5));
        cond.GE(fillZero(6));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LE(fillZero(10));
        cond.LT(fillZero(10));
        cond.GE(fillZero(5));
        cond.GE(fillZero(6));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    // conflict condition
    {
        Condition cond;
        cond.LT(fillZero(5));
        cond.GE(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.LT(fillZero(4));
        cond.GE(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.LE(fillZero(4));
        cond.GE(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.LT(fillZero(5));
        cond.LE(fillZero(4));
        cond.GE(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }
}

BOOST_AUTO_TEST_CASE(testGT)
{
    auto testData = generateData(0, 20);
    // 覆盖GT中的所有条件分支
    // no conflict condition
    {
        Condition cond;
        cond.GT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LT(fillZero(10));
        cond.GT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LE(fillZero(10));
        cond.GT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LE(fillZero(10));
        cond.LT(fillZero(10));
        cond.GT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    // update condition
    {
        Condition cond;
        cond.GT(fillZero(5));
        cond.GT(fillZero(6));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LT(fillZero(10));
        cond.GT(fillZero(5));
        cond.GT(fillZero(6));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LE(fillZero(10));
        cond.GT(fillZero(5));
        cond.GT(fillZero(6));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.LE(fillZero(10));
        cond.LT(fillZero(10));
        cond.GT(fillZero(5));
        cond.GT(fillZero(6));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    // conflict condition
    {
        Condition cond;
        cond.LT(fillZero(5));
        cond.GT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.LT(fillZero(4));
        cond.GT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.LE(fillZero(5));
        cond.GT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.LE(fillZero(4));
        cond.GT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.LT(fillZero(5));
        cond.LE(fillZero(5));
        cond.GT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }
}

BOOST_AUTO_TEST_CASE(testLT)
{
    auto testData = generateData(0, 20);
    // 覆盖LT中的所有条件分支
    // no conflict condition
    {
        Condition cond;
        cond.LT(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GT(fillZero(10));
        cond.LT(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GE(fillZero(10));
        cond.LT(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GE(fillZero(10));
        cond.GT(fillZero(10));
        cond.LT(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    // update condition
    {
        Condition cond;
        cond.LT(fillZero(16));
        cond.LT(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GT(fillZero(10));
        cond.LT(fillZero(16));
        cond.LT(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GE(fillZero(10));
        cond.LT(fillZero(16));
        cond.LT(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GE(fillZero(10));
        cond.GT(fillZero(10));
        cond.LT(fillZero(16));
        cond.LT(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    // conflict condition
    {
        Condition cond;
        cond.GT(fillZero(5));
        cond.LT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.GT(fillZero(5));
        cond.LT(fillZero(4));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.GE(fillZero(5));
        cond.LT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.GE(fillZero(5));
        cond.LT(fillZero(4));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.GT(fillZero(5));
        cond.GE(fillZero(5));
        cond.LT(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }
}

BOOST_AUTO_TEST_CASE(testLE)
{
    auto testData = generateData(0, 20);
    // 覆盖LT中的所有条件分支
    // no conflict condition
    {
        Condition cond;
        cond.LE(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GT(fillZero(10));
        cond.LE(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GE(fillZero(10));
        cond.LE(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GE(fillZero(10));
        cond.GT(fillZero(10));
        cond.LE(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    // update condition
    {
        Condition cond;
        cond.LE(fillZero(16));
        cond.LE(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GT(fillZero(10));
        cond.LE(fillZero(16));
        cond.LE(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GE(fillZero(10));
        cond.LE(fillZero(16));
        cond.LE(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.GE(fillZero(10));
        cond.GT(fillZero(10));
        cond.LE(fillZero(16));
        cond.LE(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    // conflict condition
    {
        Condition cond;
        cond.GT(fillZero(5));
        cond.LE(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.GT(fillZero(5));
        cond.LE(fillZero(4));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.GE(fillZero(5));
        cond.LE(fillZero(4));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }

    {
        Condition cond;
        cond.GT(fillZero(5));
        cond.GE(fillZero(5));
        cond.LE(fillZero(5));
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }
}

BOOST_AUTO_TEST_CASE(testNE)
{
    auto testData = generateData(0, 20);
    {
        Condition cond;
        cond.NE(fillZero(15));
        cond.NE(fillZero(16));
        cond.NE(fillZero(17));
        cond.NE(fillZero(100));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.NE(fillZero(15));
        cond.NE(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }
}

BOOST_AUTO_TEST_CASE(testEQ)
{
    auto testData = generateData(0, 20);
    {
        Condition cond;
        cond.EQ(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.EQ(fillZero(15));
        cond.EQ(fillZero(15));
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.EQ(fillZero(15));
        cond.EQ(fillZero(16));
        BOOST_CHECK(cond.isEquivalent(testData));
    }
}

BOOST_AUTO_TEST_CASE(testSTARTS_WITH)
{
    std::vector<std::string> testData = {"abc", "abcd", "abcdef", "abce"};
    {
        Condition cond;
        cond.startsWith("abcd");
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.startsWith("abc");
        cond.startsWith("abcd");
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.startsWith("abcd");
        cond.startsWith("abc");
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.startsWith("abcd");
        cond.startsWith("abce");
        BOOST_CHECK(cond.isEquivalent(testData, true));
    }
}

BOOST_AUTO_TEST_CASE(testCONTAINS)
{
    std::vector<std::string> testData = {"abc", "abcd", "abcdef", "abceabcf"};
    {
        Condition cond;
        cond.contains("abcd");
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.contains("abc");
        cond.contains("abcd");
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.contains("abc");
        cond.contains("bcf");
        BOOST_CHECK(cond.isEquivalent(testData));
    }
}

BOOST_AUTO_TEST_CASE(testENDS_WITH)
{
    std::vector<std::string> testData = {"cba", "dcba", "fedcba", "ecba"};
    {
        Condition cond;
        cond.endsWith("dcba");
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.endsWith("cba");
        cond.endsWith("dcba");
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.endsWith("dcba");
        cond.endsWith("cba");
        BOOST_CHECK(cond.isEquivalent(testData));
    }

    {
        Condition cond;
        cond.endsWith("dcba");
        cond.endsWith("ecba");
        BOOST_CHECK(cond.isEquivalent(testData));
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos