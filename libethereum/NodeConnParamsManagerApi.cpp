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
 * @file: NodeConnParamsManagerApi.cpp
 * @author: toxotguo
 * 
 * @date: 2018
 */

#include "NodeConnParamsManagerApi.h"
#include "NodeConnParamsManager.h"
#include "NodeConnParamsManagerSSL.h"
#include <libdevcore/FileSystem.h>

using namespace std;
using namespace dev;
using namespace eth;

dev::eth::NodeConnParamsManagerApi& NodeConnManagerSingleton::GetInstance()
{
	if (dev::getSSL() == SSL_SOCKET_V2)
	{
		static dev::eth::NodeConnParamsManagerSSL nodeConnParamsManagerSSL;
		return nodeConnParamsManagerSSL;
	}
	else{
		
		static dev::eth::NodeConnParamsManager nodeConnParamsManager(contentsString(getConfigPath()));
		return nodeConnParamsManager;
	}
}