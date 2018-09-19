
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
/** @file RLPXSocket.cpp
 * @author toxotguo
 * @date 2018
 *
 * * @ author: yujiechen
 * @ date: 2018-09-17
 * @ modification: rename RLPXSocket.cpp to Socket.cpp
 */
#include "Socket.h"

ba::ssl::context Socket::m_sslContext(ba::ssl::context::tlsv12);
bool Socket::m_isInit = false;
