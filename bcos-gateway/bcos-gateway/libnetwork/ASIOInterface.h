/* Backward-compatibility shim: the implementation moved to the standalone bcos-network
 * library (bcos-network/ASIOInterface.h, namespace bcos::network). This header keeps the old
 * include path and the bcos::gateway names alive for libp2p and the gateway tests, and
 * preserves the old transitive include of libnetwork/Socket.h.
 */
#pragma once

#include "bcos-gateway/libnetwork/Socket.h"
#include <bcos-network/ASIOInterface.h>

namespace bcos::gateway
{
using bcos::network::ASIOInterface;
}  // namespace bcos::gateway
