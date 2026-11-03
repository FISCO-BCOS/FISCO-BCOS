/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file DnsDiscovery.h
 * @brief EIP-1459 DNS node lists: enrtree:// URLs, signed `enrtree-root:v1` TXT
 *        records, hash-addressed `enrtree-branch:` / `enr:` / `enrtree://` entries,
 *        walked through an injectable resolver (production: libresolv).
 * @date 2026/11/3
 */
#pragma once

#include "Enr.h"
#include <bcos-utilities/Common.h>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace bcos::devp2p::disc
{
// RFC 4648 §6 base32 (standard alphabet) without padding, as dnsdisc uses for hashes and
// link public keys.
std::string base32Encode(bytesConstRef _data);
std::optional<bcos::bytes> base32Decode(std::string_view _text);

// enrtree://<base32(compressed pubkey)>@<fqdn>
struct EnrTreeLink
{
    std::string domain;
    bcos::bytes publicKey;  // 64 bytes, uncompressed
    std::string text;       // the URL as written
};
std::optional<EnrTreeLink> parseEnrTreeUrl(std::string_view _url);

// enrtree-root:v1 e=<enr-root> l=<link-root> seq=<n> sig=<base64url 65 bytes>
struct EnrTreeRoot
{
    std::string enrRoot;
    std::string linkRoot;
    uint64_t seq{0};
    bcos::bytes signature;  // 65 bytes (r || s || v)
};
std::optional<EnrTreeRoot> parseEnrTreeRoot(std::string_view _txt);
// Signature over keccak256("enrtree-root:v1 e=.. l=.. seq=..") by the link key.
bool verifyEnrTreeRoot(EnrTreeRoot const& _root, bytesConstRef _publicKey64);
// The subdomain of an entry: base32(keccak256(content)[0:16]).
std::string enrTreeEntryHash(std::string_view _content);

class DnsResolverInterface
{
public:
    using Ptr = std::shared_ptr<DnsResolverInterface>;
    virtual ~DnsResolverInterface() = default;
    // All TXT records of `_name` (each record's character-strings concatenated); empty
    // when the name does not exist. Throws on resolver failure.
    virtual std::vector<std::string> lookupTxt(std::string const& _name) = 0;
};

// libresolv (res_nquery) based resolver.
class ResolvDnsResolver : public DnsResolverInterface
{
public:
    std::vector<std::string> lookupTxt(std::string const& _name) override;
};

class DnsDiscovery
{
public:
    using Ptr = std::shared_ptr<DnsDiscovery>;
    // Invoked for every verified node record (on the calling / discovery thread).
    using RecordSink = std::function<void(Enr const&)>;

    struct SyncReport
    {
        size_t trees{0};    // trees walked (configured + linked)
        size_t records{0};  // records delivered this pass
        size_t links{0};    // link entries followed
        size_t errors{0};   // trees that failed (resolver error, bad root, bad signature)
        uint64_t lastSeq{0};
    };

    static constexpr size_t c_maxTrees = 16;             // configured + linked trees per sync
    static constexpr size_t c_maxEntriesPerTree = 2000;  // dnsdisc leaves walked per tree

    DnsDiscovery(std::vector<std::string> _urls, DnsResolverInterface::Ptr _resolver,
        RecordSink _sink = nullptr);
    ~DnsDiscovery();
    DnsDiscovery(DnsDiscovery const&) = delete;
    DnsDiscovery& operator=(DnsDiscovery const&) = delete;

    // Resolve every tree root once; walk a tree when its root seq advanced since the
    // last pass (entries already seen are served from the hash-keyed cache). Blocking.
    SyncReport syncOnce();

    // Run syncOnce on a dedicated thread now and every `_recheckInterval`.
    void start(std::chrono::seconds _recheckInterval = std::chrono::minutes(10));
    void stop();
    bool running() const { return m_running; }

    // Every record collected so far, by node id.
    std::vector<Enr> records() const;
    SyncReport lastReport() const;
    std::vector<std::string> const& urls() const { return m_urls; }

private:
    struct Tree
    {
        EnrTreeLink link;
        uint64_t seq{0};
        bool synced{false};
        std::vector<EnrTreeLink> links;  // the link subtree as of `seq`
    };
    void syncTree(Tree& _tree, SyncReport& _report, std::vector<EnrTreeLink>& _newLinks);
    // The verified content of the entry `_hash` under `_domain` (cache first).
    std::optional<std::string> resolveEntry(std::string const& _domain, std::string const& _hash);

    std::vector<std::string> m_urls;
    DnsResolverInterface::Ptr m_resolver;
    RecordSink m_sink;

    mutable std::mutex m_mutex;
    std::map<std::string, Tree> m_trees;              // by domain
    std::map<std::string, std::string> m_entryCache;  // hash -> content
    std::map<bcos::h256, Enr> m_records;              // by node id
    SyncReport m_lastReport;

    std::mutex m_runMutex;
    std::condition_variable m_wake;
    bool m_running{false};
    std::thread m_thread;
};
}  // namespace bcos::devp2p::disc
