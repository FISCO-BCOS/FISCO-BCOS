/*
    @CopyRight: This file is part of cpp-ethereum.

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
/** @file
 * Fixture class for boost output when running testeth
 */

#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>


using namespace std;
using namespace dev;
using namespace dev::test;
/// print case name when running a test-suite loading TestOutputHelperFixture
void TestOutputHelper::initTest(size_t _maxTests)
{
    m_currentTestName = "n/a";
    m_currentTestFileName = string();
    m_timer = Timer();
    m_currentTestCaseName = boost::unit_test::framework::current_test_case().p_name;
    std::cout << "===== Test Case : " + m_currentTestCaseName << "=====" << std::endl;
    m_maxTests = _maxTests;
    m_currTest = 0;
}

/// release resources when testing finished
void TestOutputHelper::finishTest()
{
    execTimeName res;
    res.first = m_timer.elapsed();
    res.second = caseName();
    std::cout << "#### Run " << res.second << " time elapsed: " << res.first << std::endl;
    m_execTimeResults.push_back(res);
}