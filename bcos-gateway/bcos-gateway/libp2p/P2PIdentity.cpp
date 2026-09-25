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
 * @file P2PIdentity.cpp
 * @brief FISCO-BCOS node-identity extraction, moved out of libnetwork's Host behind the
 *        PeerIdentity seam (see P2PIdentity.h). The extraction, admission and self-info logic
 *        below is the former Host::newVerifyCallback / Host::obtainNodeInfo / Host::p2pInfo
 *        certificate logic, unchanged in behavior except that extraction now produces a
 *        P2PInfo directly instead of round-tripping through a '#'-separated string.
 */

#include "bcos-gateway/libp2p/P2PIdentity.h"
#include "bcos-gateway/libp2p/Common.h"
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <openssl/x509.h>
#include <utility>
#include <vector>

namespace bcos::gateway
{
std::string obtainCommonNameFromSubject(std::string const& subject)
{
    std::vector<std::string> fields;
    boost::split(fields, subject, boost::is_any_of("/"), boost::token_compress_on);
    for (auto field : fields)
    {
        std::size_t pos = field.find("CN");
        if (pos != std::string::npos)
        {
            std::vector<std::string> cn_fields;
            boost::split(cn_fields, field, boost::is_any_of("="), boost::token_compress_on);
            /// use the whole fields as the common name
            if (cn_fields.size() < 2)
            {
                return field;
            }
            /// return real common name
            return cn_fields[1];
        }
    }
    return subject;
}

P2PPeerIdentity::P2PPeerIdentity(bcos::crypto::Hash::Ptr hashImpl, x509PubHandler pubHandler,
    x509PubHandler pubHandlerWithoutExtInfo)
  : m_hashImpl(std::move(hashImpl)),
    m_pubHandler(std::move(pubHandler)),
    m_pubHandlerWithoutExtInfo(std::move(pubHandlerWithoutExtInfo))
{}

IdentityToken P2PPeerIdentity::newIdentitySlot()
{
    return std::make_shared<P2PInfo>();
}

std::shared_ptr<P2PInfo> P2PPeerIdentity::p2pInfoOf(IdentityToken const& token)
{
    return std::static_pointer_cast<P2PInfo>(token);
}

PeerIdentity::Verdict P2PPeerIdentity::verifyPeer(X509* cert, IdentityToken const& identitySlot)
{
    auto peerInfo = p2pInfoOf(identitySlot);
    if (!peerInfo)
    {
        P2P_IDENTITY_LOG(ERROR) << LOG_DESC("verifyPeer: identity slot is not a P2PInfo");
        return Verdict::Reject;
    }
    P2PInfo& identityOut = *peerInfo;
    // For compatibility, p2p communication between nodes still uses the old public key
    // analysis method
    std::string rawPub;
    if (!m_pubHandler(cert, rawPub))
    {
        // no identity extractable from this certificate: take no position — the generic chain
        // policy decides, and a handshake that never produced an identity is dropped afterwards
        // (empty p2pID)
        return Verdict::Skip;
    }
    int crit = 0;
    auto* basic = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, &crit, NULL);
    if (!basic)
    {
        P2P_IDENTITY_LOG(INFO) << LOG_DESC("Get ca basic failed");
        return Verdict::Skip;
    }

    /// ignore ca
    if (basic->ca)
    {
        // ca or agency certificate
        P2P_IDENTITY_LOG(TRACE) << LOG_DESC("Ignore CA certificate");
        BASIC_CONSTRAINTS_free(basic);
        return Verdict::Skip;
    }

    BASIC_CONSTRAINTS_free(basic);

    // The new public key analysis method is used for black and white lists
    std::string pubWithoutExtInfo;
    if (!m_pubHandlerWithoutExtInfo(cert, pubWithoutExtInfo))
    {
        return Verdict::Skip;
    }
    pubWithoutExtInfo = boost::to_upper_copy(pubWithoutExtInfo);

    // If the node ID exists in the black and white lists at the same time, the black list
    // takes precedence
    if (m_peerBlacklist.has(pubWithoutExtInfo))
    {
        P2P_IDENTITY_LOG(INFO) << LOG_DESC("NodeID in certificate blacklist")
                               << LOG_KV("nodeID", P2PNodeID(pubWithoutExtInfo).abridged());
        return Verdict::Reject;
    }

    if (!m_peerWhitelist.has(pubWithoutExtInfo))
    {
        P2P_IDENTITY_LOG(INFO) << LOG_DESC("NodeID is not in certificate whitelist")
                               << LOG_KV("nodeID", P2PNodeID(pubWithoutExtInfo).abridged());
        return Verdict::Reject;
    }

    identityOut.rawP2pID = std::move(rawPub);
    bcos::crypto::HashType p2pIDHash = m_hashImpl->hash(bcos::bytesConstRef(
        (bcos::byte const*)identityOut.rawP2pID.data(), identityOut.rawP2pID.size()));
    // the p2pID, hash(rawP2pID)
    identityOut.p2pID = std::string(p2pIDHash.begin(), p2pIDHash.end());
    identityOut.p2pIDWithoutExtInfo = std::move(pubWithoutExtInfo);

    /// get issuer name / subject name
    const char* issuerName = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
    const char* certName = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
    identityOut.agencyName = obtainCommonNameFromSubject(issuerName);
    identityOut.nodeName = obtainCommonNameFromSubject(certName);
    OPENSSL_free((void*)issuerName);
    OPENSSL_free((void*)certName);

    P2P_IDENTITY_LOG(INFO) << LOG_DESC("verifyPeer: peer identity extracted")
                           << LOG_KV("p2pid", printShortP2pID(identityOut.p2pID))
                           << LOG_KV("rawP2pID", printShortP2pID(identityOut.rawP2pID));
    return Verdict::Accept;
}

P2PInfo P2PPeerIdentity::selfIdentity(X509* cert)
{
    P2PInfo info;

    /// get issuer name
    const char* issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
    std::string issuerName(issuer);

    /// get subject name
    const char* subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
    std::string subjectName(subject);

    /// get p2pID
    std::string nodeIDOut;
    if (m_pubHandler(cert, nodeIDOut))
    {
        info.p2pID = boost::to_upper_copy(nodeIDOut);
        P2P_IDENTITY_LOG(INFO) << LOG_DESC("Get node information from cert")
                               << LOG_KV("shortP2pid", printShortP2pID(info.p2pID))
                               << LOG_KV("rawP2pID", printShortP2pID(info.rawP2pID));
    }

    std::string nodeIDOutWithoutExtInfo;
    if (m_pubHandlerWithoutExtInfo(cert, nodeIDOutWithoutExtInfo))
    {
        info.p2pIDWithoutExtInfo = boost::to_upper_copy(nodeIDOutWithoutExtInfo);
        P2P_IDENTITY_LOG(INFO) << LOG_DESC("Get node information without ext info from cert")
                               << LOG_KV("p2pid without ext info", info.p2pIDWithoutExtInfo);
    }

    /// fill in the node informations
    info.agencyName = obtainCommonNameFromSubject(issuerName);
    info.nodeName = obtainCommonNameFromSubject(subjectName);
    /// free resources
    OPENSSL_free((void*)issuer);
    OPENSSL_free((void*)subject);
    return info;
}

void P2PPeerIdentity::setPeerBlacklist(PeerBlackWhitelist _peerBlacklist)
{
    m_peerBlacklist = std::move(_peerBlacklist);
}

PeerBlackWhitelist& P2PPeerIdentity::peerBlacklist()
{
    return m_peerBlacklist;
}

void P2PPeerIdentity::setPeerWhitelist(PeerBlackWhitelist _peerWhitelist)
{
    m_peerWhitelist = std::move(_peerWhitelist);
}

PeerBlackWhitelist& P2PPeerIdentity::peerWhitelist()
{
    return m_peerWhitelist;
}
}  // namespace bcos::gateway
