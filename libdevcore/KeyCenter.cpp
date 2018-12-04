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
 * @brief : keycenter for disk encrytion
 * @author: jimmyshi
 * @date: 2018-12-03
 */

#include "KeyCenter.h"
#include "Common.h"
#include "Exceptions.h"
#include "easylog.h"

using namespace std;
using namespace dev;

const std::string KeyCenter::getDataKey(const std::string& _cypherDataKey)
{
    // Fake it
    return "01234567012345670123456701234567";
};

const std::string KeyCenter::generateCypherDataKey()
{
    // Fake it
    return std::string("0123456701234567012345670123456") + std::to_string(utcTime() % 10);
}

void KeyCenter::setUrl(const std::string& _url)
{
    LOG(DEBUG) << "[KeyCenter] Instance url: " << m_url << endl;
    m_url = _url;
}

KeyCenter& KeyCenter::instance()
{
    static KeyCenter ins;
    return ins;
}