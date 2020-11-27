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
/** @file ECDSASignature.cpp
 * @author xingqiangbai
 * @date 2020-04-27
 */

#include "ECDSASignature.h"
#include "AES.h"
#include "Common.h"
#include "CryptoInterface.h"
#include "Exceptions.h"
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/pwdbased.h>
#include <cryptopp/sha.h>
#include <libethcore/Exceptions.h>
#include <libutilities/Common.h>
#include <libutilities/Exceptions.h>
#include <libutilities/RLP.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <secp256k1_sha256.h>
#include <mutex>

using namespace std;
using namespace bcos;
using namespace bcos::crypto;


static const u256 c_secp256k1n(
    "115792089237316195423570985008687907852837564279074904382605163141518161494337");

static const unsigned VBase = 27;

void bcos::crypto::ECDSASignature::encode(RLPStream& _s) const noexcept
{
    _s << (byte)(v + VBase) << (u256)r << (u256)s;
}

std::vector<unsigned char> bcos::crypto::ECDSASignature::asBytes() const
{
    std::vector<unsigned char> data;
    data.resize(65);
    memcpy(data.data(), r.data(), 32);
    memcpy(data.data() + 32, s.data(), 32);
    data[64] = v;
    return data;
}

bool bcos::crypto::ECDSASignature::isValid() const noexcept
{
    if (s > c_secp256k1n / 2)
    {
        return false;
    }
    static const h256 s_max{"0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"};
    static const h256 s_zero;

    return (v <= 1 && r > s_zero && s > s_zero && r < s_max && s < s_max);
}

namespace
{
/**
 * @brief : init secp256k1_context globally(maybe for secure consider)
 * @return secp256k1_context const* : global static secp256k1_context
 */
secp256k1_context const* getSecp256k1Ctx()
{
    static std::unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)> s_ctx{
        secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY),
        &secp256k1_context_destroy};
    return s_ctx.get();
}
}  // namespace

std::shared_ptr<crypto::Signature> bcos::ecdsaSignatureFromRLP(RLP const& _rlp, size_t _start)
{
    byte v = _rlp[_start++].toInt<byte>() - VBase;
    u256 r = _rlp[_start++].toInt<u256>();
    u256 s = _rlp[_start++].toInt<u256>();
    return std::make_shared<ECDSASignature>(r, s, v);
}
std::shared_ptr<crypto::Signature> bcos::ecdsaSignatureFromBytes(std::vector<unsigned char> _data)
{
    if (_data.size() != 65)
    {  // ecdsa signature must be 65 bytes
        return nullptr;
    }
    h256 r(_data.data(), h256::ConstructorType::FromPointer);
    h256 s(_data.data() + 32, h256::ConstructorType::FromPointer);
    auto v = _data[64];
    return std::make_shared<ECDSASignature>(r, s, v);
}

std::shared_ptr<crypto::Signature> bcos::ecdsaSign(KeyPair const& _keyPair, h256 const& _hash)
{
    auto* ctx = getSecp256k1Ctx();
    secp256k1_ecdsa_recoverable_signature rawSig;
    if (!secp256k1_ecdsa_sign_recoverable(
            ctx, &rawSig, _hash.data(), _keyPair.secret().data(), nullptr, nullptr))
        return {};

    unsigned char s[65];
    int v = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, s, &v, &rawSig);

    auto ss = std::make_shared<ECDSASignature>();
    ss->r = h256(s, h256::ConstructorType::FromPointer);
    ss->s = h256(s + 32, h256::ConstructorType::FromPointer);
    ss->v = static_cast<byte>(v);
    if (ss->s > c_secp256k1n / 2)
    {
        ss->v = static_cast<byte>(ss->v ^ 1);
        ss->s = h256(c_secp256k1n - u256(ss->s));
    }
    assert(ss->s <= c_secp256k1n / 2);
    return ss;
}

bool bcos::ecdsaVerify(h512 const& _p, std::shared_ptr<crypto::Signature> _s, h256 const& _hash)
{
    // TODO: Verify w/o recovery (if faster).
    if (!_p)
        return false;
    return _p == ecdsaRecover(_s, _hash);
}

Public bcos::ecdsaRecover(std::shared_ptr<crypto::Signature> _s, h256 const& _message)
{
    auto _sig = dynamic_pointer_cast<ECDSASignature>(_s);
    if (!_sig)
    {
        return h512{};
    }
    int v = _sig->v;
    if (v > 3)
        return {};

    auto* ctx = getSecp256k1Ctx();
    secp256k1_ecdsa_recoverable_signature rawSig;
    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(
            ctx, &rawSig, _sig->asBytes().data(), v))
        return {};

    secp256k1_pubkey rawPubkey;
    if (!secp256k1_ecdsa_recover(ctx, &rawPubkey, &rawSig, _message.data()))
        return {};

    std::array<byte, 65> serializedPubkey;
    size_t serializedPubkeySize = serializedPubkey.size();
    secp256k1_ec_pubkey_serialize(
        ctx, serializedPubkey.data(), &serializedPubkeySize, &rawPubkey, SECP256K1_EC_UNCOMPRESSED);
    assert(serializedPubkeySize == serializedPubkey.size());
    // Expect single byte header of value 0x04 -- uncompressed public key.
    assert(serializedPubkey[0] == 0x04);
    // Create the Public skipping the header.
    return Public{&serializedPubkey[1], Public::ConstructorType::FromPointer};
}

pair<bool, bytes> bcos::ecRecover(bytesConstRef _in)
{
    struct
    {
        h256 hash;
        h256 v;
        h256 r;
        h256 s;
    } in;

    memcpy(&in, _in.data(), min(_in.size(), sizeof(in)));

    h256 ret;
    u256 v = (u256)in.v;
    if (v >= 27 && v <= 28)
    {
        auto sig = std::make_shared<ECDSASignature>(in.r, in.s, (byte)((int)v - 27));
        if (sig->isValid())
        {
            try
            {
                if (Public rec = ecdsaRecover(sig, in.hash))
                {
                    ret = bcos::keccak256(rec);
                    memset(ret.data(), 0, 12);
                    return {true, ret.asBytes()};
                }
            }
            catch (...)
            {
            }
        }
    }
    return {true, {}};
}
