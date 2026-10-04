/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file P2PIdentity.h
 * @brief The gateway's PeerIdentity implementation (the identity seam of libnetwork's Host,
 *        see libnetwork/PeerIdentity.h). FISCO-BCOS derives node identity from the TLS
 *        certificate: the certificate public key hex is the raw node id, the short p2pID is
 *        hash(rawP2pID), and the agency/node names come from the certificate issuer/subject.
 *        Certificate black/white-list admission lives here too — the lists are owned by this
 *        class and can be updated live on config reload (Service::updatePeerBlacklist).
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libnetwork/PeerIdentity.h"
#include "bcos-gateway/libp2p/PeerBlackWhitelist.h"
#include <memory>
#include <set>
#include <string>

namespace bcos::gateway
{
using x509PubHandler = std::function<bool(X509* x509, std::string& pubHex)>;

/// obtain the common name from the subject:
/// the subject format is: /CN=xx/O=xxx/OU=xxx/ commonly
std::string obtainCommonNameFromSubject(std::string const& subject);

class P2PPeerIdentity : public PeerIdentity
{
public:
    P2PPeerIdentity(bcos::crypto::Hash::Ptr hashImpl, x509PubHandler pubHandler,
        x509PubHandler pubHandlerWithoutExtInfo);

    IdentityToken newIdentitySlot() override;
    Verdict verifyPeer(X509* cert, IdentityToken const& identitySlot) override;

    /// The identity tokens this implementation produces/fills hold a P2PInfo.
    static std::shared_ptr<P2PInfo> p2pInfoOf(IdentityToken const& token);

    /// Extract this host's own identity from its own certificate (no admission policy).
    /// libp2p-only: the generic seam does not need it (Service::localP2pInfo uses it).
    P2PInfo selfIdentity(X509* cert);

    // Certificate black/white lists, consulted by verifyPeer. Updated live on config reload.
    void setPeerBlacklist(PeerBlackWhitelist _peerBlacklist);
    PeerBlackWhitelist& peerBlacklist();
    void setPeerWhitelist(PeerBlackWhitelist _peerWhitelist);
    PeerBlackWhitelist& peerWhitelist();

private:
    bcos::crypto::Hash::Ptr m_hashImpl;
    x509PubHandler m_pubHandler;
    x509PubHandler m_pubHandlerWithoutExtInfo;

    // Peer black/white list, disabled by default (blocks/rejects no one)
    PeerBlackWhitelist m_peerBlacklist{
        PeerBlackWhitelist::Type::Blacklist, std::set<std::string>{}};
    PeerBlackWhitelist m_peerWhitelist{
        PeerBlackWhitelist::Type::Whitelist, std::set<std::string>{}};
};
}  // namespace bcos::gateway
