/* Backward-compatibility shim: the implementation moved to the standalone bcos-network
 * library (bcos-network/PeerIdentity.h, namespace bcos::network). This header keeps the old
 * include path and the bcos::gateway names alive for libp2p and the gateway tests.
 */
#pragma once

#include <bcos-network/PeerIdentity.h>
// P2PIdentity.cpp (libp2p, unmodifiable) relies on BASIC_CONSTRAINTS from <openssl/x509v3.h>,
// which it previously obtained transitively from the libnetwork sources sharing its gateway
// unity-build batch. Restore that transitive include here.
#include <openssl/x509v3.h>

namespace bcos::gateway
{
using bcos::network::IdentityToken;
using bcos::network::PeerIdentity;
}  // namespace bcos::gateway
