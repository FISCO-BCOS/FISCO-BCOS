/*
 * Common.h
 *
 *      Author: ancelmo
 */

#pragma once

#include <bcos-framework/libutilities/Common.h>
#include <bcos-framework/libutilities/Exceptions.h>
#include <bcos-gateway/libnetwork/Common.h>

namespace bcos
{
namespace gateway
{
#define P2PMSG_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][P2PMessage]"
#define P2PSESSION_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][P2PSession]"
#define SERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][Service]"
}  // namespace gateway
}  // namespace bcos
