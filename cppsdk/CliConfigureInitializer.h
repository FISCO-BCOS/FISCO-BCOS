/*
    This file is part of FISCO-BCOS-CHAIN.

    FISCO-BCOS-CHAIN is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS-CHAIN is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS-CHAIN.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 *  @author jimmyshi
 *  @modify first draft
 *  @date 2018-11-30
 */

#pragma once

#include "Common.h"
#include <libconfig/GlobalConfigure.h>

namespace dev
{
namespace channel
{

void getCliGlobalConfig(const boost::property_tree::ptree& _pt);
uint32_t getCliVersionNumber(const std::string& _version);


}  // namespace initializer

void cliVersion();
}  // namespace dev
