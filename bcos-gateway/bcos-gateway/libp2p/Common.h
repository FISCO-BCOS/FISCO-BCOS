/*
 * Common.h
 *
 *      Author: ancelmo
 */

#pragma once

#include "bcos-gateway/libnetwork/Common.h"
#include <bcos-utilities/BoostLog.h>

namespace bcos
{
namespace gateway
{
#define P2PMSG_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][P2PMessage]"
#define P2PSESSION_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][P2PSession]"
#define SERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][Service]"
#define SERVICE2_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][Service2]"
#define SERVICE_ROUTER_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][Router]"

}  // namespace gateway
}  // namespace bcos
