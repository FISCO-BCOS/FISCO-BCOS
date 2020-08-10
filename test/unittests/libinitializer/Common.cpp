/**
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
 *
 * @brief: unit test for common
 *
 * @file Common.cpp
 * @author: xingqiangbai
 * @date 2019-05-01
 */


#include "libinitializer/GlobalConfigureInitializer.h"
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace dev;
using namespace dev::initializer;

namespace dev
{
namespace test_GlobalConfig
{
struct GlobalConfigFixture
{
    GlobalConfigFixture() { pt.put("storage_security.enable", false); }
    boost::property_tree::ptree pt;
};

BOOST_FIXTURE_TEST_SUITE(GlobalConfig, GlobalConfigFixture)

BOOST_AUTO_TEST_CASE(test_initGlobalConfig)
{
    initGlobalConfig(pt);
    BOOST_CHECK(g_BCOSConfig.version() == RC1_VERSION);
    BOOST_CHECK(g_BCOSConfig.compressEnabled() == true);
    BOOST_CHECK(g_BCOSConfig.diskEncryption.enable == false);
    BOOST_CHECK(g_BCOSConfig.diskEncryption.keyCenterIP == "");
    BOOST_CHECK(g_BCOSConfig.diskEncryption.keyCenterPort == 20000);
    BOOST_CHECK(g_BCOSConfig.diskEncryption.cipherDataKey == "");
    pt.put("compatibility.supported_version", "2.0.3");
    initGlobalConfig(pt);
    BOOST_CHECK(g_BCOSConfig.version() == static_cast<VERSION>(33555200));
    pt.put("compatibility.supported_version", "2.5.3");
    initGlobalConfig(pt);
    BOOST_CHECK(g_BCOSConfig.version() == static_cast<VERSION>(0x02050300));
    pt.put("compatibility.supported_version", "22.5.3");
    BOOST_CHECK_THROW(initGlobalConfig(pt), std::exception);
    pt.put("compatibility.supported_version", "2.0.0-rc2");
    initGlobalConfig(pt);
    BOOST_CHECK(g_BCOSConfig.version() == RC2_VERSION);
    pt.put("compatibility.supported_version", "2.0.0-rc3");
    initGlobalConfig(pt);
    BOOST_CHECK(g_BCOSConfig.version() == RC3_VERSION);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test_GlobalConfig
}  // namespace dev
