/** @file Common.h
 * Backward-compatibility shim: the implementation moved to the standalone bcos-network
 * library (bcos-network/Common.h, namespace bcos::network). This header keeps the old
 * include path and the bcos::gateway names alive for libp2p and the gateway tests.
 * The unscoped enums' enumerators are aliased individually — a using-declaration of the
 * enum TYPE alone does not bring them into this namespace.
 */
#pragma once

#include <bcos-network/Common.h>

namespace bcos::gateway
{
using bcos::network::MessageDecodeStatus;
using bcos::network::MESSAGE_ERROR;
using bcos::network::MESSAGE_INCOMPLETE;

using bcos::network::c_compressThreshold;
using bcos::network::c_zstdCompressLevel;

using bcos::network::DisconnectReason;
using bcos::network::BadProtocol;
using bcos::network::ClientQuit;
using bcos::network::DisconnectRequested;
using bcos::network::DuplicatePeer;
using bcos::network::IdleWaitTimeout;
using bcos::network::InBlacklistReason;
using bcos::network::IncompatibleProtocol;
using bcos::network::LocalIdentity;
using bcos::network::NegotiateFailed;
using bcos::network::NoDisconnect;
using bcos::network::NotInWhitelistReason;
using bcos::network::NullIdentity;
using bcos::network::PingTimeout;
using bcos::network::TCPError;
using bcos::network::TooManyPeers;
using bcos::network::UnexpectedIdentity;
using bcos::network::UselessPeer;
using bcos::network::UserReason;

using bcos::network::P2PExceptionType;
using bcos::network::ALL;
using bcos::network::ConnectError;
using bcos::network::Disconnect;
using bcos::network::DuplicateSession;
using bcos::network::InQPSOverflow;
using bcos::network::NetworkTimeout;
using bcos::network::NotInWhitelist;
using bcos::network::OutBWOverflow;
using bcos::network::P2PExceptionTypeCnt;
using bcos::network::ProtocolError;
using bcos::network::Success;

using bcos::network::Options;
using bcos::network::P2pID;
using bcos::network::P2pIDs;

using bcos::network::errinfo_errorCode;
using bcos::network::errorCodeOf;
using bcos::network::makeNetworkException;
using bcos::network::NetworkException;
using bcos::network::operator!;
using bcos::network::reasonOf;
using bcos::network::toError;
}  // namespace bcos::gateway
