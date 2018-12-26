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
    g_BCOSConfig.diskEncryption.enable = _pt.get<bool>("disk_encryption.enable", false);
    g_BCOSConfig.diskEncryption.keyCenterIP =
        _pt.get<std::string>("disk_encryption.keycenter_ip", "");
    g_BCOSConfig.diskEncryption.keyCenterPort = _pt.get<int>("disk_encryption.keycenter_port", 0);
    g_BCOSConfig.diskEncryption.cipherDataKey =
        _pt.get<std::string>("disk_encryption.cipher_data_key", "");

    INITIALIZER_LOG(DEBUG) << "[#initDiskEncryptionConfig] [enable/url/key]:  "
                           << g_BCOSConfig.diskEncryption.enable << "/"
                           << g_BCOSConfig.diskEncryption.keyCenterIP << ":"
                           << g_BCOSConfig.diskEncryption.keyCenterPort << "/"
                           << g_BCOSConfig.diskEncryption.cipherDataKey << std::endl;
}
