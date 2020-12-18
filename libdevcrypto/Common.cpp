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
/** @file Common.cpp
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com>
 * @author Asherli
 * @date 2018
 */

#include "Common.h"
#include "AES.h"
#include "CryptoInterface.h"
#include "Exceptions.h"
#include "libdevcrypto/sm2/sm2.h"
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/pwdbased.h>
#include <cryptopp/sha.h>
#include <libprotocol/Exceptions.h>
#include <libutilities/RLP.h>

using namespace std;
using namespace bcos;
using namespace bcos::crypto;

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

/**
 * @brief obtain address from public key
 *        by adding the last 20Bytes of keccak256(public key)
 * @param _public : the public key need to convert to address
 * @return Address : the converted address
 */
Address bcos::toAddress(Public const& _public)
{
    return right160(crypto::Hash(_public.ref()));
}

Address bcos::toAddress(Secret const& _secret)
{
    return toAddress(toPublic(_secret));
}

/**
 * @brief : 1.serialize (_from address, nonce) into rlpStream
 *          2.calculate the keccak256 of serialized (_from, nonce)
 *          3.obtaining the last 20Bytes of the keccak256 as address
 *          (mainly used for contract address generating)
 * @param _from : address that sending this transaction
 * @param _nonce : random number
 * @return Address : generated address
 */
Address bcos::toAddress(Address const& _from, u256 const& _nonce)
{
    return right160(crypto::Hash(rlpList(_from, _nonce)));
}

Public bcos::toPublic(Secret const& _secret)
{
    if (!g_BCOSConfig.SMCrypto())
    {
        auto* ctx = getCtx();
        secp256k1_pubkey rawPubkey;
        // Creation will fail if the secret key is invalid.
        if (!secp256k1_ec_pubkey_create(ctx, &rawPubkey, _secret.data()))
            return {};
        std::array<byte, 65> serializedPubkey;
        size_t serializedPubkeySize = serializedPubkey.size();
        secp256k1_ec_pubkey_serialize(ctx, serializedPubkey.data(), &serializedPubkeySize,
            &rawPubkey, SECP256K1_EC_UNCOMPRESSED);
        assert(serializedPubkeySize == serializedPubkey.size());
        // Expect single byte header of value 0x04 -- uncompressed public key.
        assert(serializedPubkey[0] == 0x04);
        // Create the Public skipping the header.
        return Public{&serializedPubkey[1], Public::ConstructorType::FromPointer};
    }
    else
    {
        string pri = *toHexString(bytesConstRef{_secret.data(), 32});
        string pub = SM2::getInstance().priToPub(pri);
        return h512(*fromHexString(pub));
    }
}

KeyPair KeyPair::create()
{
    while (true)
    {
        KeyPair keyPair(Secret::generateRandomFixedBytes());
        if (keyPair.address())
            return keyPair;
    }
}

Secret Nonce::next()
{
    Guard l(x_value);
    if (!m_value)
    {
        m_value = Secret::generateRandomFixedBytes();
        if (!m_value)
            BOOST_THROW_EXCEPTION(InvalidState());
    }


    if (g_BCOSConfig.SMCrypto())
    {
        m_value = sm3Secure(m_value.ref());
    }
    else
    {
        m_value = keccak256Secure(m_value.ref());
    }

    return crypto::Hash(~m_value);
}
