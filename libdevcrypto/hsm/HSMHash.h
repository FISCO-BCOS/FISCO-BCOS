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
#include "CryptoProvider.h"
#include "sdf/SDFCryptoProvider.h"
#include <libdevcore/FixedHash.h>
#include <libdevcore/vector_ref.h>
#include <string>
#if FISCO_SDF
using namespace hsm;
using namespace hsm::sdf;
#define SDR_OK 0x0
#endif

namespace dev
{
namespace crypto
{
unsigned int SDFSM3(bytesConstRef _input, bytesRef o_output);

inline h256 SDFSM3(bytesSec const& _input);
/// Calculate SM3-256 hash of the given input, returning as a 256-bit hash.
inline h256 SDFSM3(bytesConstRef _input)
{
    CryptoProvider& provider = SDFCryptoProvider::GetInstance();
    h256 ret;
    unsigned int code = SDFSM3(_input, ret.ref());
    if (code != SDR_OK)
    {
        throw provider.GetErrorMessage(code);
    }
    return ret;
}
inline h256 SDFSM3(bytes const& _input)
{
    return SDFSM3(bytesConstRef(&_input));
}
inline h256 SDFSM3(std::string const& _input)
{
    return SDFSM3(bytesConstRef(_input));
}
template <unsigned N>
inline h256 SDFSM3(FixedHash<N> const& _input)
{
    return SDFSM3(_input.ref());
}


}  // namespace crypto
}  // namespace dev
