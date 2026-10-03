/* Backward-compatibility shim: the implementation moved to the standalone bcos-network
 * library (bcos-network/FrameMeta.h, namespace bcos::network). This header keeps the old
 * include path and the bcos::gateway names alive for libp2p and the gateway tests.
 */
#pragma once

#include <bcos-network/FrameMeta.h>

namespace bcos::gateway
{
using bcos::network::FRAME_TAKE_BUFFER_THRESHOLD;
using bcos::network::FrameDecoder;
using bcos::network::FrameMeta;
}  // namespace bcos::gateway
