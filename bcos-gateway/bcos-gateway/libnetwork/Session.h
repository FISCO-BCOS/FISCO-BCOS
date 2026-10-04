/** @file Session.h
 * Backward-compatibility shim: the implementation moved to the standalone bcos-network
 * library (bcos-network/Session.h, namespace bcos::network). This header keeps the old
 * include path and the bcos::gateway names alive for libp2p and the gateway tests, and
 * preserves the old transitive includes (ASIOInterface/Common/FrameMeta/Socket, plus Host
 * at the bottom like the original header).
 */
#pragma once

#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/FrameMeta.h"
#include "bcos-gateway/libnetwork/Socket.h"
#include <bcos-network/Session.h>

namespace bcos::gateway
{
using bcos::network::BasicSession;
using bcos::network::BasicSessionFactory;
using bcos::network::Payload;
using bcos::network::SessionRecvBuffer;

namespace detail
{
using bcos::network::detail::send;
}  // namespace detail
}  // namespace bcos::gateway

// The original header included Host.h at the bottom; keep the transitive availability.
#include "bcos-gateway/libnetwork/Host.h"
