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
/** @file HashCommonFunc.cpp
 * @author Alex Leverington <nessence@gmail.com> Asherli
 * @date 2018
 *
 * The FixedHash fixed-size "hash" container type.
 */
#include "Hash.h"
#include <secp256k1_sha256.h>
using namespace std;
using namespace dev;

// add sha2 -- sha256 to this file begin
h256 dev::standardSha256(bytesConstRef _input) noexcept
{
    secp256k1_sha256_t ctx;
    secp256k1_sha256_initialize(&ctx);
    secp256k1_sha256_write(&ctx, _input.data(), _input.size());
    h256 hash;
    secp256k1_sha256_finalize(&ctx, hash.data());
    return hash;
}
