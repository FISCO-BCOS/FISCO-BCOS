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
/** @file Common.h
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com> asherli
 * @date 2018
 *
 * Ethereum-specific data structures & algorithms.
 */

#pragma once

#include <libdevcore/Address.h>
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/RLP.h>
#include <mutex>


namespace dev
{
using Secret = SecureFixedHash<32>;

/// A public key: 64 bytes.
/// @NOTE This is not endian-specific; it's just a bunch of bytes.
using Public = h512;

/// A signature: 65 bytes: r: [0, 32), s: [32, 64), v: 64.
/// @NOTE This is not endian-specific; it's just a bunch of bytes.
#ifdef FISCO_GM
using Signature = h1024;
using VType = h512;
using NumberVType = u512;
static const u512 VBase = 0;
#else
using Signature = h520;
using VType = byte;
using NumberVType = byte;
static const unsigned VBase = 27;
#endif
struct SignatureStruct
{
    SignatureStruct() = default;
    SignatureStruct(Signature const& _s);
    SignatureStruct(h256 const& _r, h256 const& _s, VType _v);
    SignatureStruct(u256 const& _r, u256 const& _s, NumberVType _v);
    static std::pair<bool, bytes> ecRecover(bytesConstRef _in);
    // SignatureStruct(VType _v, h256 const& _r, h256 const& _s);
    void encode(RLPStream& _s) const noexcept;
    void check() const noexcept;
    operator Signature() const { return *(Signature const*)this; }

    /// @returns true if r,s,v values are valid, otherwise false
    bool isValid() const noexcept;

    h256 r;
    h256 s;
    VType v;
};

// m_vrs.rlp()

/// A vector of secrets.
using Secrets = std::vector<Secret>;


NumberVType getVFromRLP(RLP const& _txRLPField);

/// Convert a secret key into the public key equivalent.
/// if convert failed, assertion failed
Public toPublic(Secret const& _secret);

/// Convert a public key to address.
/// right160(sha3(_public.ref()))
Address toAddress(Public const& _public);

/// Convert a secret key into address of public key equivalent.
/// @returns 0 if it's not a valid secret key.
Address toAddress(Secret const& _secret);

// Convert transaction from and nonce to address.
// for contract adddress generation
Address toAddress(Address const& _from, u256 const& _nonce);

// /// Encrypts plain text using Public key.(asymmetric encryption)
// void encrypt(Public const& _k, bytesConstRef _plain, bytes& o_cipher);

// /// Decrypts cipher using Secret key.(asymmetric decryption)
// bool decrypt(Secret const& _k, bytesConstRef _cipher, bytes& o_plaintext);


/// Recovers Public key from signed message hash.
Public recover(Signature const& _sig, h256 const& _hash);

/// Returns siganture of message hash.
// SM2 is a non-deterministic signature algorithm. Even with the same hash and private key, will
// obtained different [r] and [s] values.
Signature sign(Secret const& _k, h256 const& _hash);

/// Verify signature.

bool verify(Public const& _k, Signature const& _s, h256 const& _hash);

/// Simple class that represents a "key pair".
/// All of the data of the class can be regenerated from the secret key (m_secret) alone.
/// Actually stores a tuplet of secret, public and address (the right 160-bits of the public).
class KeyPair
{
public:
    /// Normal constructor - populates object from the given secret key.
    /// If the secret key is invalid the constructor succeeds, but public key
    /// and address stay "null".
    KeyPair(Secret const& _sec) : m_secret(_sec), m_public(toPublic(_sec))
    {
        // Assign address only if the secret key is valid.
        if (m_public)
            m_address = toAddress(m_public);
    }

    KeyPair() {}

    /// Create a new, randomly generated object.
    static KeyPair create();

    Secret const& secret() const { return m_secret; }

    /// Retrieve the public key.
    Public const& pub() const { return m_public; }

    /// Retrieve the associated address of the public key.
    Address const& address() const { return m_address; }

    bool operator==(KeyPair const& _c) const { return m_public == _c.m_public; }
    bool operator!=(KeyPair const& _c) const { return m_public != _c.m_public; }

private:
    Secret m_secret;
    Public m_public;
    Address m_address;
};

namespace crypto
{
DEV_SIMPLE_EXCEPTION(InvalidState);

/**
 * @brief Generator for non-repeating nonce material.
 * The Nonce class should only be used when a non-repeating nonce
 * is required and, in its current form, not recommended for signatures.
 * This is primarily because the key-material for signatures is
 * encrypted on disk whereas the seed for Nonce is not.
 * Thus, Nonce's primary intended use at this time is for networking
 * where the key is also stored in plaintext.
 */
class Nonce
{
public:
    /// Returns the next nonce (might be read from a file).
    static Secret get()
    {
        static Nonce s;
        return s.next();
    }

private:
    Nonce() = default;

    /// @returns the next nonce.
    Secret next();

    std::mutex x_value;
    Secret m_value;
};
}  // namespace crypto
}  // namespace dev
