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
/** @file SDFSM3Hash.cpp
 * @author maggie
 * @date 2021-02-01
 */

#include "HSMHash.h"
#include "SDFCryptoProvider.h"
#include "CryptoProvider.h"
#include <libdevcore/FixedHash.h>
using namespace std;
using namespace dev;
using namespace dev::crypto;

unsigned int dev::crypto::SDFSM3(bytesConstRef _input, bytesRef o_output)
{
    // FIXME: What with unaligned memory?
    if (o_output.size() != 32)
        return false;
    // get provider
    CryptoProvider& provider = SDFCryptoProvider::GetInstance();
    unsigned int uiHashResultLen;
    unsigned int code = provider.Hash(nullptr, SM3, _input.data(), _input.size(),
        (unsigned char*)o_output.data(), &uiHashResultLen);
    return code;
}