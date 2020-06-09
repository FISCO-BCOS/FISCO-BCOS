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
/** @file Hash.cpp
 * @author Asherli
 * @date 2018
 */

#include "libdevcrypto/SM3Hash.h"
#include "libdevcrypto/sm3/sm3.h"
#include <libdevcore/RLP.h>
#include <libethcore/Exceptions.h>
#include <secp256k1_sha256.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace std;
using namespace dev;

bool dev::sm3(bytesConstRef _input, bytesRef o_output)
{  // FIXME: What with unaligned memory?
    if (o_output.size() != 32)
        return false;

    SM3Hash::getInstance().sm3(
        (const unsigned char*)_input.data(), _input.size(), (unsigned char*)o_output.data());
    return true;
}
