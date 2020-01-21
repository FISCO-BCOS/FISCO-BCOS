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
 * @author Asherli
 * @date 2018
 */

#include "libdevcrypto/AES.h"
#include "libdevcrypto/Common.h"
#include "libdevcrypto/Exceptions.h"
#include "libdevcrypto/Hash.h"
#include "libdevcrypto/gm/sm2/sm2.h"
#include <cryptopp/modes.h>
#include <cryptopp/pwdbased.h>
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libethcore/Exceptions.h>
using namespace std;
using namespace dev;
using namespace dev::crypto;


SignatureStruct::SignatureStruct(Signature const& _s)
{
    *(Signature*)this = _s;
}

// SignatureStruct::SignatureStruct(VType _v, h256 const& _r, h256 const& _s) : r(_r), s(_s), v(_v)
// {}

SignatureStruct::SignatureStruct(h256 const& _r, h256 const& _s, VType _v) : r(_r), s(_s), v(_v) {}
SignatureStruct::SignatureStruct(u256 const& _r, u256 const& _s, NumberVType _v)
{
    r = _r;
    s = _s;
    v = _v;
}

pair<bool, bytes> SignatureStruct::ecRecover(bytesConstRef _in)
{
    struct
    {
        h256 hash;
        h512 v;
        h256 r;
        h256 s;
    } in;
    memcpy(&in, _in.data(), min(_in.size(), sizeof(in)));

    SignatureStruct sig(in.r, in.s, in.v);
    if (sig.isValid())
    {
        try
        {
            if (Public rec = recover(sig, in.hash))
            {
                h256 ret = dev::sha3(rec);
                memset(ret.data(), 0, 12);
                return {true, ret.asBytes()};
            }
        }
        catch (...)
        {
        }
    }
    return {true, {}};
}


void SignatureStruct::encode(RLPStream& _s) const noexcept
{
    _s << (VType)(v) << (u256)r << (u256)s;
}


void SignatureStruct::check() const noexcept
{
    if (!v)
        BOOST_THROW_EXCEPTION(eth::InvalidSignature());
}

bool dev::SignatureStruct::isValid() const noexcept
{
    static const h256 s_max{"0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"};
    static const h256 s_zero;

    return (v >= h512(1) && r > s_zero && s > s_zero && r < s_max && s < s_max);
}

NumberVType dev::getVFromRLP(RLP const& _txRLPField)
{
    // Note: Since sdk encode v into bytes in sdk, the nodes must decode the v into bytes
    // firstly, otherwise the transaction will decode failed in some cases
    VType vBytes = _txRLPField.toHash<VType>();
    NumberVType v = NumberVType(vBytes) - VBase;
    return v;
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

Signature dev::sign(Secret const& _k, h256 const& _hash)
{
    string pri = toHex(bytesConstRef{_k.data(), 32});
    string r = "", s = "";
    if (!SM2::getInstance().sign((const char*)_hash.data(), h256::size, pri, r, s))
    {
        return Signature{};
    }
    // std::cout << "Gmcommon.cpp line 255 Secret====" << pri << std::endl;
    string pub = SM2::getInstance().priToPub(pri);
    // std::cout <<"_hash:"<<toHex(_hash.asBytes())<<"gmSign:"<< r + s + pub;
    bytes byteSign = fromHex(r + s + pub);
    // std::cout <<"sign toHex:"<<toHex(byteSign)<<" sign toHexLen:"<<toHex(byteSign).length();
    return Signature{byteSign};
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

bool dev::verify(Public const& _p, Signature const& _s, h256 const& _hash)
{
    string signData = toHex(_s.asBytes());
    string pub = toHex(_p.asBytes());
    pub = "04" + pub;
    bool lresult = SM2::getInstance().verify(
        signData, signData.length(), (const char*)_hash.data(), h256::size, pub);
    return lresult;
}
