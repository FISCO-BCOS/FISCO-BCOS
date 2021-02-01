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
/** @file SDFSM3Hash.h
 * @author maggie
 * @date 2021-02-01
 */

#pragma once
#include <libdevcore/FixedHash.h>
#include <libdevcore/vector_ref.h>
#include <string>
namespace dev
{
inline SecureFixedHash<32> SDFSM3Secure(std::string const& _input);
inline SecureFixedHash<32> sm3(bytesSec const& _input);
}  // namespace dev
