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
/** @file SM2Signature.cpp
 * @author xingqiangbai
 * @date 2020-04-27
 */

#include "SM2Signature.h"
#include "libdevcrypto/AES.h"
#include "libdevcrypto/Common.h"
#include "libdevcrypto/Exceptions.h"
#include "libdevcrypto/SM3Hash.h"
#include "libdevcrypto/sm2/sm2.h"
#include <cryptopp/modes.h>
#include <cryptopp/pwdbased.h>
#include <libdevcore/Address.h>
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libethcore/Exceptions.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <secp256k1_sha256.h>
#include <mutex>

using namespace std;
using namespace dev;
using namespace dev::crypto;

static const u512 VBase = 0;
using VType = h512;
using NumberVType = u512;

void SM2Signature::encode(RLPStream& _s) const noexcept
{
    _s << (VType)(v) << (u256)r << (u256)s;
}

std::vector<unsigned char> dev::crypto::SM2Signature::asBytes() const
{
    std::vector<unsigned char> data;
    data.resize(128);
    memcpyWithCheck(data.data(), data.size(), r.data(), 32);
    memcpyWithCheck(data.data() + 32, data.size() - 32, s.data(), 32);
    memcpyWithCheck(data.data() + 64, data.size() - 64, v.data(), 64);
    return data;
}

bool dev::crypto::SM2Signature::isValid() const noexcept
{
    static const h256 s_max{"0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"};
    static const h256 s_zero;

    return (v >= h512(1) && r > s_zero && s > s_zero && r < s_max && s < s_max);
}

std::shared_ptr<crypto::Signature> dev::sm2SignatureFromRLP(RLP const& _rlp, size_t _start)
{
    h512 v = _rlp[_start++].toHash<h512>();
    u256 r = _rlp[_start++].toInt<u256>();
    u256 s = _rlp[_start++].toInt<u256>();
    return std::make_shared<SM2Signature>(r, s, v);
}
std::shared_ptr<crypto::Signature> dev::sm2SignatureFromBytes(std::vector<unsigned char> _data)
{
    if (_data.size() != 128)
    {  // sm2 signature must be 128 bytes
        return nullptr;
    }
    h256 r(_data.data(), FixedHash<32>::ConstructFromPointerType::ConstructFromPointer);
    h256 s(_data.data() + 32, FixedHash<32>::ConstructFromPointerType::ConstructFromPointer);
    h512 v(_data.data() + 64, FixedHash<64>::ConstructFromPointerType::ConstructFromPointer);
    return std::make_shared<SM2Signature>(r, s, v);
}

std::shared_ptr<crypto::Signature> dev::sm2Sign(KeyPair const& _keyPair, h256 const& _hash)
{
    string pri = toHex(bytesConstRef{_keyPair.secret().data(), 32});
    h256 r(0);
    h256 s(0);
    if (!SM2::getInstance().sign((const char*)_hash.data(), h256::size, pri, r.data(), s.data()))
    {
        return nullptr;
    }
    return make_shared<SM2Signature>(r, s, _keyPair.pub());
}

bool dev::sm2Verify(h512 const& _p, std::shared_ptr<crypto::Signature> _s, h256 const& _hash)
{
    if (!_s)
    {
        return false;
    }
    auto signData = _s->asBytes();
    bool lresult = SM2::getInstance().verify(
        signData.data(), signData.size(), _hash.data(), h256::size, _p.data());
    return lresult;
}

h512 dev::sm2Recover(std::shared_ptr<crypto::Signature> _s, h256 const& _message)
{
    auto _sig = dynamic_pointer_cast<SM2Signature>(_s);
    if (!_sig)
    {
        return h512{};
    }

    if (!_sig->isValid())
    {
        return h512{};
    }
    if (sm2Verify(_sig->v, _sig, _message))
    {
        return _sig->v;
    }
    return h512{};
}

pair<bool, bytes> dev::recover(bytesConstRef _in)
{
    struct
    {
        h256 hash;
        h512 v;
        h256 r;
        h256 s;
    } in;
    memcpyWithCheck(&in, sizeof(h256) * 4, _in.data(), min(_in.size(), sizeof(in)));
    auto sig = std::make_shared<SM2Signature>(in.r, in.s, in.v);
    if (!sig->isValid())
    {
        return {false, {}};
    }
    auto rec = sm2Recover(sig, in.hash);
    h256 ret = sm3(rec);
    memset(ret.data(), 0, 12);
    return {true, ret.asBytes()};
}
