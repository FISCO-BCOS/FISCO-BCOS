/** @file Socket.h
 * Backward-compatibility shim: the implementation moved to the standalone bcos-network
 * library (bcos-network/Socket.h, namespace bcos::network). This header keeps the old
 * include path and the bcos::gateway names alive for libp2p and the gateway tests, and
 * preserves the old transitive include of libnetwork/Common.h.
 */
#pragma once

#include "bcos-gateway/libnetwork/Common.h"
// The original libnetwork/Socket.h transitively provided GatewayTypeDef.h (P2PInfo etc.);
// keep that for existing includers.
#include <bcos-framework/gateway/GatewayTypeDef.h>
#include <bcos-network/Socket.h>

namespace bcos::gateway
{
using bcos::network::BasicSocket;
using bcos::network::PlainSocket;
using bcos::network::Socket;
}  // namespace bcos::gateway
