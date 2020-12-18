/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file: TestOutputHelper.h
 * Fixture class for boost output when running testeth
 */

#pragma once
#include <libconfig/GlobalConfigure.h>
#include <libdevcrypto/CryptoInterface.h>
#include <libutilities/Common.h>
#include <boost/filesystem.hpp>

namespace bcos
{
namespace test
{
class TestOutputHelper
{
public:
    /**
     * @brief : single pattern , construct a static TestOutputHelper
     */
    static TestOutputHelper& get()
    {
        static TestOutputHelper instance;
        return instance;
    }
    TestOutputHelper(TestOutputHelper const&) = delete;
    void operator=(TestOutputHelper const&) = delete;
    /**
     * @brief : init every test-suite by printting the name of cases
     * @param _maxTests
     */
    void initTest(size_t _maxTests = 1);

    /**
     * @brief : release resources when test-suit finished
     */
    void finishTest();

    void setCurrentTestFile(boost::filesystem::path const& _name) { m_currentTestFileName = _name; }
    void setCurrentTestName(std::string const& _name) { m_currentTestName = _name; }
    std::string const& testName() { return m_currentTestName; }
    std::string const& caseName() { return m_currentTestCaseName; }
    boost::filesystem::path const& testFile() { return m_currentTestFileName; }

private:
    TestOutputHelper() {}
    size_t m_currTest;
    size_t m_maxTests;
    std::string m_currentTestName;
    std::string m_currentTestCaseName;
    boost::filesystem::path m_currentTestFileName;
    typedef std::pair<double, std::string> execTimeName;
    std::vector<execTimeName> m_execTimeResults;
    int64_t m_startTime;
};

class TestOutputHelperFixture
{
public:
    /// init test-suite fixture
    TestOutputHelperFixture() { TestOutputHelper::get().initTest(); }
    /// release test-suite fixture
    ~TestOutputHelperFixture() { TestOutputHelper::get().finishTest(); }
};

class SM_CryptoTestFixture
{
public:
    SM_CryptoTestFixture() : m_originUseSMCrypto(g_BCOSConfig.SMCrypto())
    {
        g_BCOSConfig.setUseSMCrypto(true);
        if (!m_originUseSMCrypto)
        {
            crypto::initSMCrypto();
        }
        TestOutputHelper::get().initTest();
    }

    ~SM_CryptoTestFixture()
    {
        TestOutputHelper::get().finishTest();
        if (!m_originUseSMCrypto)
        {
            crypto::initCrypto();
        }
        g_BCOSConfig.setUseSMCrypto(m_originUseSMCrypto);
    }

private:
    bool m_originUseSMCrypto;
};

}  // namespace test
}  // namespace bcos
