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

#include <libdevcrypto/Common.h>
#include <libutilities/Exceptions.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#define INITIALIZER_LOG(LEVEL) LOG(LEVEL) << "[INITIALIZER]"
#define ERROR_OUTPUT std::cout << "[INITIALIZER]"

namespace bcos
{
DERIVE_BCOS_EXCEPTION(InvalidListenPort);
DERIVE_BCOS_EXCEPTION(ListenPortIsUsed);
DERIVE_BCOS_EXCEPTION(ConfigNotExist);
DERIVE_BCOS_EXCEPTION(InvalidConfig);
DERIVE_BCOS_EXCEPTION(InitFailed);


namespace initializer
{
DERIVE_BCOS_EXCEPTION(GroupExists);
DERIVE_BCOS_EXCEPTION(GenesisNotExists);
DERIVE_BCOS_EXCEPTION(GroupConfigNotExists);

DERIVE_BCOS_EXCEPTION(GenesisExists);
DERIVE_BCOS_EXCEPTION(GroupConfigExists);
DERIVE_BCOS_EXCEPTION(GroupGeneratorException);
DERIVE_BCOS_EXCEPTION(InvalidGenesisGroupID);
DERIVE_BCOS_EXCEPTION(InvalidGenesisTimestamp);
DERIVE_BCOS_EXCEPTION(InvalidGenesisNodeid);
DERIVE_BCOS_EXCEPTION(NotConnectGenesisNode);

inline bool isValidPort(int port)
{
    if (port <= 1024 || port > 65535)
        return false;
    return true;
}
}  // namespace initializer
}  // namespace bcos
