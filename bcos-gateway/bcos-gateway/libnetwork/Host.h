/** @file Host.h
 * Backward-compatibility shim: the implementation moved to the standalone bcos-network
 * library (bcos-network/Host.h, namespace bcos::network). This header keeps the old
 * include path and the bcos::gateway names alive for libp2p and the gateway tests, and
 * preserves the old transitive includes (ASIOInterface/Common/PeerIdentity/Session/Socket).
 */
#pragma once

#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/PeerIdentity.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/Socket.h"
// The original libnetwork/Host.h transitively provided GatewayTypeDef.h (P2PInfo etc.);
// keep that for existing includers.
#include <bcos-framework/gateway/GatewayTypeDef.h>
#include <bcos-network/Host.h>

namespace bcos::gateway
{
using bcos::network::Host;

namespace detail
{
using bcos::network::detail::HandshakeSlotGuard;
using bcos::network::detail::SessionSlotGuard;
using bcos::network::detail::tryAcquireHandshakeSlotGuard;
}  // namespace detail
}  // namespace bcos::gateway
