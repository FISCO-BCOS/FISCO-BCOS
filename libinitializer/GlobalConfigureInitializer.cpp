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

using namespace std;
using namespace dev;
using namespace dev::initializer;

void GlobalConfigureInitializer::initConfig(const boost::property_tree::ptree& _pt)
{
    // TODO: rename keycenter to key-manager, disk encryption to data secure
    g_BCOSConfig.diskEncryption.enable = _pt.get<bool>("data_secure.enable", false);
    g_BCOSConfig.diskEncryption.keyCenterIP =
        _pt.get<std::string>("data_secure.key_manager_ip", "");
    g_BCOSConfig.diskEncryption.keyCenterPort = _pt.get<int>("data_secure.key_manager_port", 0);
    g_BCOSConfig.diskEncryption.cipherDataKey =
        _pt.get<std::string>("data_secure.cipher_data_key", "");

    INITIALIZER_LOG(DEBUG) << LOG_BADGE("initKeyManagerConfig") << LOG_DESC("load configuration")
                           << LOG_KV("enable", g_BCOSConfig.diskEncryption.enable)
                           << LOG_KV("url.IP", g_BCOSConfig.diskEncryption.keyCenterIP)
                           << LOG_KV("url.port",
                                  std::to_string(g_BCOSConfig.diskEncryption.keyCenterPort))
                           << LOG_KV("key", g_BCOSConfig.diskEncryption.cipherDataKey);
}
