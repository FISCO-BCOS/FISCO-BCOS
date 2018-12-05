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
/** @file Common.cpp
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */


#include "libdevcrypto/AES.h"
#include "libdevcrypto/Common.h"
#include "libdevcrypto/CryptoPP.h"
#include "libdevcrypto/ECDHE.h"
#include "libdevcrypto/Exceptions.h"
#include "libdevcrypto/Hash.h"
#include "libdevcrypto/gm/sm2/sm2.h"
#include <cryptopp/modes.h>
#include <cryptopp/pwdbased.h>
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libethcore/Exceptions.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <secp256k1_sha256.h>
using namespace std;
using namespace dev;
using namespace dev::crypto;


SignatureStruct::SignatureStruct(Signature const& _s)
{
    *(Signature*)this = _s;
}

SignatureStruct::SignatureStruct(h256 const& _r, h256 const& _s, VType _v) : r(_r), s(_s), v(_v) {}

void SignatureStruct::encode(RLPStream& _s) const noexcept
{
    _s << (VType)(v) << (u256)r << (u256)s;
}
SignatureStruct::SignatureStruct(RLP const& rlp)
{
    v = rlp[7].toInt<u512>();  // 7
    r = rlp[8].toInt<u256>();  // 8
    s = rlp[9].toInt<u256>();  // 9

    if (!v)
        BOOST_THROW_EXCEPTION(eth::InvalidSignature());
}

void SignatureStruct::check() const noexcept
{
    if (!v)
        BOOST_THROW_EXCEPTION(eth::InvalidSignature());
}

namespace
{
/**
 * @brief : init secp256k1_context globally(maybe for secure consider)
 * @return secp256k1_context const* : global static secp256k1_context
 */
secp256k1_context const* getCtx()
{
    static std::unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)> s_ctx{
        secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY),
        &secp256k1_context_destroy};
    return s_ctx.get();
}

}  // namespace

bool dev::SignatureStruct::isValid() const noexcept
{
    static const h256 s_max{"0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"};
    static const h256 s_zero;

    return (v >= h512(1) && r > s_zero && s > s_zero && r < s_max && s < s_max);
}

/**
 * @brief : obtain public key according to secret key
 * @param _secret : the data of secret key
 * @return Public : created public key; if create failed, assertion failed
 */
Public dev::toPublic(Secret const& _secret)
{
    string pri = toHex(bytesConstRef{_secret.data(), 32});
    string pub = SM2::getInstance().priToPub(pri);
    return Public(fromHex(pub));
}

/**
 * @brief obtain address from public key
 *        by adding the last 20Bytes of sha3(public key)
 * @param _public : the public key need to convert to address
 * @return Address : the converted address
 */
Address dev::toAddress(Public const& _public)
{
    return right160(sha3(_public.ref()));
}

Address dev::toAddress(Secret const& _secret)
{
    return toAddress(toPublic(_secret));
}

/**
 * @brief : 1.serialize (_from address, nonce) into rlpStream
 *          2.calculate the sha3 of serialized (_from, nonce)
 *          3.obtaining the last 20Bytes of the sha3 as address
 *          (mainly used for contract address generating)
 * @param _from : address that sending this transaction
 * @param _nonce : random number
 * @return Address : generated address
 */
Address dev::toAddress(Address const& _from, u256 const& _nonce)
{
    return right160(sha3(rlpList(_from, _nonce)));
}

/**
 * @brief : encrypt plain text with public key
 * @param _k : public key
 * @param _plain : plain text need to be encrypted
 * @param o_cipher : encrypted ciper text
 */
void dev::encrypt(Public const& _k, bytesConstRef _plain, bytes& o_cipher)
{
    bytes io = _plain.toBytes();
    Secp256k1PP::get()->encrypt(_k, io);
    o_cipher = std::move(io);
}

/**
 * @brief : decrypt ciper text with secret key
 * @param _k : private key used to decrypt
 * @param _cipher : ciper text
 * @param o_plaintext : decrypted plain text
 * @return true : decrypt succeed
 * @return false : decrypt failed(maybe key or ciper text is invalid)
 */
bool dev::decrypt(Secret const& _k, bytesConstRef _cipher, bytes& o_plaintext)
{
    bytes io = _cipher.toBytes();
    Secp256k1PP::get()->decrypt(_k, io);
    if (io.empty())
        return false;
    o_plaintext = std::move(io);
    return true;
}

void dev::encryptECIES(Public const& _k, bytesConstRef _plain, bytes& o_cipher)
{
    encryptECIES(_k, bytesConstRef(), _plain, o_cipher);
}

void dev::encryptECIES(
    Public const& _k, bytesConstRef _sharedMacData, bytesConstRef _plain, bytes& o_cipher)
{
    bytes io = _plain.toBytes();
    Secp256k1PP::get()->encryptECIES(_k, _sharedMacData, io);
    o_cipher = std::move(io);
}

bool dev::decryptECIES(Secret const& _k, bytesConstRef _cipher, bytes& o_plaintext)
{
    return decryptECIES(_k, bytesConstRef(), _cipher, o_plaintext);
}

bool dev::decryptECIES(
    Secret const& _k, bytesConstRef _sharedMacData, bytesConstRef _cipher, bytes& o_plaintext)
{
    bytes io = _cipher.toBytes();
    if (!Secp256k1PP::get()->decryptECIES(_k, _sharedMacData, io))
        return false;
    o_plaintext = std::move(io);
    return true;
}

void dev::encryptSym(Secret const& _k, bytesConstRef _plain, bytes& o_cipher)
{
    // TODO: @alex @subtly do this properly.
    encrypt(KeyPair(_k).pub(), _plain, o_cipher);
}

bool dev::decryptSym(Secret const& _k, bytesConstRef _cipher, bytes& o_plain)
{
    // TODO: @alex @subtly do this properly.
    return decrypt(_k, _cipher, o_plain);
}

std::pair<bytes, h128> dev::encryptSymNoAuth(SecureFixedHash<16> const& _k, bytesConstRef _plain)
{
    h128 iv(Nonce::get().makeInsecure());
    return make_pair(encryptSymNoAuth(_k, iv, _plain), iv);
}

bytes dev::encryptAES128CTR(bytesConstRef _k, h128 const& _iv, bytesConstRef _plain)
{
    return bytes();
}

bytesSec dev::decryptAES128CTR(bytesConstRef _k, h128 const& _iv, bytesConstRef _cipher)
{
    return bytesSec();
}

Public dev::recover(Signature const& _sig, h256 const& _message)
{
    SignatureStruct sign(_sig);
    if (!sign.isValid())
    {
        return Public{};
    }
    if (verify(sign.v, _sig, _message))
    {
        return sign.v;
    }
    return Public{};
    // return sign.pub;
}

static const u256 c_secp256k1n(
    "115792089237316195423570985008687907852837564279074904382605163141518161494337");

Signature dev::sign(Secret const& _k, h256 const& _hash)
{
    string pri = toHex(bytesConstRef{_k.data(), 32});
    string r = "", s = "";
    if (!SM2::getInstance().sign((const char*)_hash.data(), h256::size, pri, r, s))
    {
        return Signature{};
    }
    string pub = SM2::getInstance().priToPub(pri);
    // LOG(DEBUG) <<"_hash:"<<toHex(_hash.asBytes())<<"gmSign:"<< r + s + pub;
    bytes byteSign = fromHex(r + s + pub);
    // LOG(DEBUG)<<"sign toHex:"<<toHex(byteSign)<<" sign toHexLen:"<<toHex(byteSign).length();
    return Signature{byteSign};
}

bool dev::verify(Public const& _p, Signature const& _s, h256 const& _hash)
{
    string signData = toHex(_s.asBytes());
    // LOG(DEBUG)<<"verify signData:"<<signData;
    // LOG(DEBUG)<<"_hash:"<<toHex(_hash.asBytes());
    string pub = toHex(_p.asBytes());
    pub = "04" + pub;
    // LOG(DEBUG)<<"verify pub:"<<pub;
    bool lresult = SM2::getInstance().verify(
        signData, signData.length(), (const char*)_hash.data(), h256::size, pub);
    // LOG(DEBUG)<<"verify lresult:"<<lresult;
    // assert(lresult);
    return lresult;
}


KeyPair::KeyPair(Secret const& _sec) : m_secret(_sec), m_public(toPublic(_sec))
{
    // Assign address only if the secret key is valid.
    if (m_public)
        m_address = toAddress(m_public);
}

KeyPair KeyPair::create()
{
    while (true)
    {
        KeyPair keyPair(Secret::random());
        if (keyPair.address())
            return keyPair;
    }
}


Secret Nonce::next()
{
    Guard l(x_value);
    if (!m_value)
    {
        m_value = Secret::random();
        if (!m_value)
            BOOST_THROW_EXCEPTION(InvalidState());
    }
    m_value = sha3Secure(m_value.ref());
    return sha3(~m_value);
}

bool dev::crypto::ecdh::agree(Secret const& _s, Public const& _r, Secret& o_s)
{
    return Secp256k1PP::get()->agree(_s, _r, o_s);
}

bytes ecies::kdf(Secret const& _z, bytes const& _s1, unsigned kdByteLen)
{
    auto reps = ((kdByteLen + 7) * 8) / 512;
    // SEC/ISO/Shoup specify counter size SHOULD be equivalent
    // to size of hash output, however, it also notes that
    // the 4 bytes is okay. NIST specifies 4 bytes.
    std::array<byte, 4> ctr{{0, 0, 0, 1}};
    bytes k;
    secp256k1_sha256_t ctx;
    for (unsigned i = 0; i <= reps; i++)
    {
        secp256k1_sha256_initialize(&ctx);
        secp256k1_sha256_write(&ctx, ctr.data(), ctr.size());
        secp256k1_sha256_write(&ctx, _z.data(), Secret::size);
        secp256k1_sha256_write(&ctx, _s1.data(), _s1.size());
        // append hash to k
        std::array<byte, 32> digest;
        secp256k1_sha256_finalize(&ctx, digest.data());

        k.reserve(k.size() + h256::size);
        move(digest.begin(), digest.end(), back_inserter(k));

        if (++ctr[3] || ++ctr[2] || ++ctr[1] || ++ctr[0])
            continue;
    }

    k.resize(kdByteLen);
    return k;
}
