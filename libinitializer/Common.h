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
/** @file Common.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#pragma once

#include <libdevcore/Exceptions.h>
#include <libdevcrypto/Common.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#define INITIALIZER_LOG(LEVEL) LOG(LEVEL) << "[INITIALIZER]"
#define ERROR_OUTPUT std::cout << "[INITIALIZER]"

namespace dev
{
DEV_SIMPLE_EXCEPTION(InvalidListenPort);
DEV_SIMPLE_EXCEPTION(ListenPortIsUsed);
DEV_SIMPLE_EXCEPTION(ConfigNotExist);
DEV_SIMPLE_EXCEPTION(InvalidConfig);
DEV_SIMPLE_EXCEPTION(InitFailed);
namespace initializer
{
inline bool isValidPort(int port)
{
    if (port <= 1024 || port > 65535)
        return false;
    return true;
}
}  // namespace initializer
}  // namespace dev
