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
/** @file Common.cpp
 *  @author caryliao
 *  @date 20190104
 */

#include "Common.h"

std::string dev::storage::getOutJson(int _code, std::string _msg)
{
    json_spirit::Object output;
    output.push_back(json_spirit::Pair("code", _code));
    output.push_back(json_spirit::Pair("msg", _msg));
    json_spirit::Value value(output);
    std::string str = json_spirit::write_string(value, true);
    return str;
}
