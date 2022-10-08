/*
 * Common.h
 *
 *      Author: ancelmo
 */

#pragma once

#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>

namespace bcos
{
namespace gateway
{
#define P2PMSG_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][P2PMessage]"
#define P2PSESSION_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][P2PSession]"
#define SERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][Service]"
#define SERVICE_ROUTER_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][Router]"

/// default compress threshold: 1KB
const uint64_t c_compressThreshold = 1024;
/// default zstd compress level:
const uint64_t c_zstdCompressLevel = 1;

}  // namespace gateway
}  // namespace bcos
