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
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @brief : init keycenter for disk encrytion
 * @author: jimmyshi
 * @date: 2018-12-03
 */
#pragma once
#include <libdevcore/Common.h>
#include <libsecurity/KeyCenter.h>

namespace dev
{
namespace initializer
{
class KeyCenterInitializer
{
public:
    static void init()
    {
        g_keyCenter.setIpPort(
            g_BCOSConfig.diskEncryption.keyCenterIP, g_BCOSConfig.diskEncryption.keyCenterPort);
    }
};

}  // namespace initializer
}  // namespace dev