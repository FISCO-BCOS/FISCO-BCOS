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

#include "Hash.h"
#include "Signature.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Address.h>
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <secp256k1_sha256.h>
#include <mutex>
namespace dev
{
using Secret = SecureFixedHash<32>;

/// A public key: 64 bytes.
/// @NOTE This is not endian-specific; it's just a bunch of bytes.
using Public = h512;

/// A vector of secrets.
using Secrets = std::vector<Secret>;

/// Convert a secret key into the public key equivalent.
/// if convert failed, assertion failed
Public toPublic(Secret const& _secret);

/// Convert a public key to address.
/// right160(keccak256(_public.ref()))
Address toAddress(Public const& _public);

/// Convert a secret key into address of public key equivalent.
/// @returns 0 if it's not a valid secret key.
Address toAddress(Secret const& _secret);

// Convert transaction from and nonce to address.
// for contract adddress generation
Address toAddress(Address const& _from, u256 const& _nonce);

/// Simple class that represents a "key pair".
/// All of the data of the class can be regenerated from the secret key (m_secret) alone.
/// Actually stores a tuplet of secret, public and address (the right 160-bits of the public).
class KeyPair
{
public:
    /// Normal constructor - populates object from the given secret key.
    /// If the secret key is invalid the constructor succeeds, but public key
    /// and address stay "null".
    KeyPair(Secret const& _sec) : m_secret(_sec), m_public(toPublic(_sec)),m_isInternalKey(false)
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

    /// Retrieve the key index
    unsigned int keyIndex() const { return m_keyIndex; }

    /// Get key type
    bool isInternalKey() const { return m_isInternalKey; }

    /// Set key index
    void setKeyIndex(unsigned int keyIndex)
    {
        m_keyIndex = keyIndex;
        m_isInternalKey = true;
    }

    /// Set pub by cert file
    void set_pub(const std::string& certfile);

    bool operator==(KeyPair const& _c) const { return m_public == _c.m_public; }
    bool operator!=(KeyPair const& _c) const { return m_public != _c.m_public; }

private:
    Secret m_secret;
    Public m_public;
    Address m_address;
    bool m_isInternalKey;
    unsigned int m_keyIndex;
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
