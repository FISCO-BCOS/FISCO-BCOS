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
 * (c) 2016-2019 fisco-dev contributors.
 */
/** @file Common.cpp
 *  @author bxq
 *  @date 20191218
 */
#include "Common.h"
#include "libconfig/GlobalConfigure.h"
#include <string>

using namespace std;
using namespace dev;
using namespace dev::storage;

bool dev::storage::isHashField(const std::string& _key)
{
    if (!_key.empty())
    {
        if (g_BCOSConfig.version() < RC3_VERSION)
        {
            return ((_key.substr(0, 1) != "_" && _key.substr(_key.size() - 1, 1) != "_") ||
                    (_key == STATUS));
        }
        return ((_key.substr(0, 1) != "_" && _key.substr(_key.size() - 1, 1) != "_"));
    }
    return false;
}
