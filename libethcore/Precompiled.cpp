/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Precompiled.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "Precompiled.h"
#include "libdevcrypto/ECDSASignature.h"
#include "libdevcrypto/SM2Signature.h"
#include "libdevcrypto/SM3Hash.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/Common.h>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::crypto;

PrecompiledRegistrar* PrecompiledRegistrar::s_this = nullptr;

PrecompiledExecutor const& PrecompiledRegistrar::executor(std::string const& _name)
{
    if (!get()->m_execs.count(_name))
        BOOST_THROW_EXCEPTION(ExecutorNotFound());
    return get()->m_execs[_name];
}

PrecompiledPricer const& PrecompiledRegistrar::pricer(std::string const& _name)
{
    if (!get()->m_pricers.count(_name))
        BOOST_THROW_EXCEPTION(PricerNotFound());
    return get()->m_pricers[_name];
}

namespace
{
ETH_REGISTER_PRECOMPILED(ecrecover)(bytesConstRef _in)
{
    // When supported_version> = v2.4.0, ecRecover uniformly calls the ECDSA verification function
    if (g_BCOSConfig.version() >= V2_4_0)
    {
        return dev::ecRecover(_in);
    }
    // before 2.4.0, in sm crypto mode this use sm2 recover which is a bug
    if (g_BCOSConfig.SMCrypto())
    {
        return recover(_in);
    }
    else
    {
        return dev::ecRecover(_in);
    }
}

ETH_REGISTER_PRECOMPILED(sha256)(bytesConstRef _in)
{
    // When supported_version> = v2.4.0, sha256 uniformly calls the secp sha256 function
    if (g_BCOSConfig.version() >= V2_4_0)
    {
        return {true, dev::sha256(_in).asBytes()};
    }
    // before 2.4.0, in sm crypto mode this use sm3 which is a bug
    if (g_BCOSConfig.SMCrypto())
    {
        return {true, dev::sm3(_in).asBytes()};
    }
    else
    {
        return {true, dev::sha256(_in).asBytes()};
    }
}

ETH_REGISTER_PRECOMPILED(ripemd160)(bytesConstRef _in)
{
    return {true, h256(dev::ripemd160(_in), h256::AlignRight).asBytes()};
}

ETH_REGISTER_PRECOMPILED(identity)(bytesConstRef _in)
{
    return {true, _in.toBytes()};
}

// Parse _count bytes of _in starting with _begin offset as big endian int.
// If there's not enough bytes in _in, consider it infinitely right-padded with zeroes.
bigint parseBigEndianRightPadded(bytesConstRef _in, bigint const& _begin, bigint const& _count)
{
    if (_begin > _in.count())
        return 0;
    assert(_count <= numeric_limits<size_t>::max() / 8);  // Otherwise, the return value would not
                                                          // fit in the memory.

    size_t const begin{_begin};
    size_t const count{_count};

    // crop _in, not going beyond its size
    bytesConstRef cropped = _in.cropped(begin, min(count, _in.count() - begin));

    bigint ret = fromBigEndian<bigint>(cropped);
    // shift as if we had right-padding zeroes
    assert(count - cropped.count() <= numeric_limits<size_t>::max() / 8);
    ret <<= 8 * (count - cropped.count());

    return ret;
}

ETH_REGISTER_PRECOMPILED(modexp)(bytesConstRef _in)
{
    // This is a protocol of bignumber modular
    // Described here:
    // https://github.com/ethereum/EIPs/blob/master/EIPS/eip-198.md
    bigint const baseLength(parseBigEndianRightPadded(_in, 0, 32));
    bigint const expLength(parseBigEndianRightPadded(_in, 32, 32));
    bigint const modLength(parseBigEndianRightPadded(_in, 64, 32));
    assert(modLength <= numeric_limits<size_t>::max() / 8);   // Otherwise gas should be too
                                                              // expensive.
    assert(baseLength <= numeric_limits<size_t>::max() / 8);  // Otherwise, gas should be too
                                                              // expensive.
    if (modLength == 0 && baseLength == 0)
        return {true, bytes{}};  // This is a special case where expLength can be very big.
    assert(expLength <= numeric_limits<size_t>::max() / 8);

    bigint const base(parseBigEndianRightPadded(_in, 96, baseLength));
    bigint const exp(parseBigEndianRightPadded(_in, 96 + baseLength, expLength));
    bigint const mod(parseBigEndianRightPadded(_in, 96 + baseLength + expLength, modLength));

    bigint const result = mod != 0 ? boost::multiprecision::powm(base, exp, mod) : bigint{0};

    size_t const retLength(modLength);
    bytes ret(retLength);
    toBigEndian(result, ret);

    return {true, ret};
}

namespace
{
bigint expLengthAdjust(bigint const& _expOffset, bigint const& _expLength, bytesConstRef _in)
{
    if (_expLength <= 32)
    {
        bigint const exp(parseBigEndianRightPadded(_in, _expOffset, _expLength));
        return exp ? msb(exp) : 0;
    }
    else
    {
        bigint const expFirstWord(parseBigEndianRightPadded(_in, _expOffset, 32));
        size_t const highestBit(expFirstWord ? msb(expFirstWord) : 0);
        return 8 * (_expLength - 32) + highestBit;
    }
}

bigint multComplexity(bigint const& _x)
{
    if (_x <= 64)
        return _x * _x;
    if (_x <= 1024)
        return (_x * _x) / 4 + 96 * _x - 3072;
    else
        return (_x * _x) / 16 + 480 * _x - 199680;
}
}  // namespace

ETH_REGISTER_PRECOMPILED_PRICER(modexp)(bytesConstRef _in)
{
    bigint const baseLength(parseBigEndianRightPadded(_in, 0, 32));
    bigint const expLength(parseBigEndianRightPadded(_in, 32, 32));
    bigint const modLength(parseBigEndianRightPadded(_in, 64, 32));

    bigint const maxLength(max(modLength, baseLength));
    bigint const adjustedExpLength(expLengthAdjust(baseLength + 96, expLength, _in));

    return multComplexity(maxLength) * max<bigint>(adjustedExpLength, 1) / 20;
}

}  // namespace
