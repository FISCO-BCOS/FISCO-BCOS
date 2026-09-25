/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file PeerIdentity.h
 * @brief The identity seam of Host: everything about WHO a peer is — extracting a node
 *        identity from the TLS certificate and applying admission policy on it — lives behind
 *        this interface. libnetwork itself only runs the TLS plumbing (verify-callback wiring,
 *        handshake orchestration, generic chain policy) and carries the extracted identity as
 *        an opaque IdentityToken; it never inspects the contents. The gateway's FISCO-BCOS
 *        implementation is libp2p::P2PPeerIdentity (libp2p/P2PIdentity.h), whose tokens hold
 *        a P2PInfo.
 */

#pragma once

#include <openssl/x509.h>
#include <cstdint>
#include <memory>

namespace bcos::gateway
{
/// Opaque per-connection identity object, created by the injected PeerIdentity at the start of
/// a handshake (newIdentitySlot), filled by its verifyPeer hook during the TLS handshake, and
/// handed by Host to the connection handler / connect() caller alongside the new session.
using IdentityToken = std::shared_ptr<void>;

class PeerIdentity
{
public:
    // Per-certificate verdict during the TLS handshake. OpenSSL invokes the verify callback
    // once per certificate in the presented chain, so an implementation must distinguish
    // "this certificate carries the peer identity" from "this certificate is not the peer's
    // (e.g. a CA certificate) — take no position and let the chain policy decide".
    enum class Verdict : uint8_t
    {
        Accept,  ///< identity extracted into the slot
        Reject,  ///< hard reject (e.g. cert admission lists) — fails the handshake
        Skip,    ///< no peer identity on this certificate; the generic chain policy decides
    };

    virtual ~PeerIdentity() = default;

    /// Create the empty identity slot for one handshake. Host wires it into the verify
    /// callback and, on a successful handshake, hands it over with the new session.
    virtual IdentityToken newIdentitySlot() = 0;

    /// TLS verify hook: extract the peer's identity from cert into the slot and apply
    /// admission policy. Called once per certificate in the presented chain; the last Accept
    /// wins.
    virtual Verdict verifyPeer(X509* cert, IdentityToken const& identitySlot) = 0;
};
}  // namespace bcos::gateway
