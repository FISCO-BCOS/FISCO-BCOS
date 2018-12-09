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
/** @file ECDHE.h
 * @author Asherli
 * @date 2018
 *
 * Elliptic curve Diffie-Hellman ephemeral key exchange
 */

#pragma once

#include "Common.h"

namespace dev
{
namespace crypto
{
/**
 * @brief Derive DH shared secret from EC keypairs.
 * As ephemeral keys are single-use, agreement is limited to a single occurrence.
 */
class ECDHE
{
public:
    /// Constructor (pass public key for ingress exchange).
    ECDHE() : m_ephemeral(KeyPair::create()){};
    ECDHE(KeyPair k) : m_ephemeral(k){};
    /// Public key sent to remote.
    Public pubkey() { return m_ephemeral.pub(); }

    Secret seckey() { return m_ephemeral.secret(); }

    /// Input public key for dh agreement, output generated shared secret.
    bool agree(Public const& _remoteEphemeral, Secret& o_sharedSecret) const;

protected:
    KeyPair m_ephemeral;               ///< Ephemeral keypair; generated.
    mutable Public m_remoteEphemeral;  ///< Public key of remote; parameter. Set once when agree is
                                       ///< called, otherwise immutable.
};

}  // namespace crypto
}  // namespace dev
