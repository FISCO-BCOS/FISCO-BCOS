/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 *  @author jimmyshi
 *  @modify first draft
 *  @date 2018-11-30
 */


#include "GlobalConfigureInitializer.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using namespace dev;
using namespace dev::initializer;

DEV_SIMPLE_EXCEPTION(UnknowSupportVersion);

bool dev::initializer::getVersionNumber(const string& _version, uint32_t& _versionNumber)
{
    // 0MNNPPTS, M=MAJOR N=MINOR P=PATCH T=TWEAK S=STATUS
    vector<string> versions;
    boost::split(versions, _version, boost::is_any_of("."));
    if (versions.size() != 3)
    {
        return false;
    }
    try
    {
        for (size_t i = 0; i < versions.size(); ++i)
        {
            _versionNumber += boost::lexical_cast<uint32_t>(versions[i]);
            _versionNumber <<= 8;
        }
    }
    catch (const boost::bad_lexical_cast& e)
    {
        INITIALIZER_LOG(ERROR) << LOG_KV("what", boost::diagnostic_information(e));
        return false;
    }
    return true;
}

void dev::initializer::initGlobalConfig(const boost::property_tree::ptree& _pt)
{
    /// default version is RC1
    std::string version = _pt.get<std::string>("compatibility.supported_version", "2.0.0-rc1");
    uint32_t versionNumber = 0;
    if (dev::stringCmpIgnoreCase(version, "2.0.0-rc1") == 0)
    {
        g_BCOSConfig.setSupportedVersion(version, RC1_VERSION);
    }
    else if (dev::stringCmpIgnoreCase(version, "2.0.0-rc2") == 0)
    {
        g_BCOSConfig.setSupportedVersion(version, RC2_VERSION);
    }
    else if (dev::stringCmpIgnoreCase(version, "2.0.0-rc3") == 0)
    {
        g_BCOSConfig.setSupportedVersion(version, RC3_VERSION);
    }
    else if (getVersionNumber(version, versionNumber))
    {
        g_BCOSConfig.setSupportedVersion(version, static_cast<VERSION>(versionNumber));
    }
    else
    {
        BOOST_THROW_EXCEPTION(UnknowSupportVersion() << errinfo_comment(version));
    }

    std::string sectionName = "data_secure";
    if (_pt.get_child_optional("storage_security"))
    {
        sectionName = "storage_security";
    }

    g_BCOSConfig.diskEncryption.enable = _pt.get<bool>(sectionName + ".enable", false);
    g_BCOSConfig.diskEncryption.keyCenterIP =
        _pt.get<std::string>(sectionName + ".key_manager_ip", "");
    g_BCOSConfig.diskEncryption.keyCenterPort =
        _pt.get<int>(sectionName + ".key_manager_port", 20000);
    if (!isValidPort(g_BCOSConfig.diskEncryption.keyCenterPort))
    {
        BOOST_THROW_EXCEPTION(
            InvalidPort() << errinfo_comment("P2PInitializer:  initConfig for storage_security "
                                             "failed! Invalid key_manange_port!"));
    }

    g_BCOSConfig.diskEncryption.cipherDataKey =
        _pt.get<std::string>(sectionName + ".cipher_data_key", "");

    /// compress related option, default enable
    bool enableCompress = _pt.get<bool>("p2p.enable_compress", true);
    g_BCOSConfig.setCompress(enableCompress);

    /// init version
    int64_t chainId = _pt.get<int64_t>("chain.id", 1);
    if (chainId < 0)
    {
        BOOST_THROW_EXCEPTION(
            ForbidNegativeValue() << errinfo_comment("Please set chain.id to positive!"));
    }
    g_BCOSConfig.setChainId(chainId);


    INITIALIZER_LOG(INFO) << LOG_BADGE("initKeyManagerConfig") << LOG_DESC("load configuration")
                          << LOG_KV("enable", g_BCOSConfig.diskEncryption.enable)
                          << LOG_KV("url.IP", g_BCOSConfig.diskEncryption.keyCenterIP)
                          << LOG_KV("url.port",
                                 std::to_string(g_BCOSConfig.diskEncryption.keyCenterPort))
                          << LOG_KV("key", g_BCOSConfig.diskEncryption.cipherDataKey)
                          << LOG_KV("enableCompress", g_BCOSConfig.compressEnabled())
                          << LOG_KV("compatibilityVersion", version)
                          << LOG_KV("versionNumber", g_BCOSConfig.version())
                          << LOG_KV("chainId", g_BCOSConfig.chainId());
}
