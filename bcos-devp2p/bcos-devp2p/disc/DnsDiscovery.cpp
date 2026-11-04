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
 * @file DnsDiscovery.cpp
 * @brief EIP-1459 DNS node list client.
 * @date 2026/11/3
 */
#include "DnsDiscovery.h"
#include "../rlpx/Crypto.h"
#include <bcos-utilities/BoostLog.h>
#if !defined(_WIN32)
#include <arpa/nameser.h>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#endif
#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <vector>

#define DNSDISC_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("dnsdisc")

namespace bcos::devp2p::disc
{
namespace
{
constexpr std::string_view c_rootPrefix = "enrtree-root:v1";
constexpr std::string_view c_linkPrefix = "enrtree://";
constexpr std::string_view c_branchPrefix = "enrtree-branch:";
constexpr std::string_view c_enrPrefix = "enr:";
constexpr char const* c_base32Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

// geth dnsdisc isValidHash: 12..32 decoded bytes of valid base32.
bool isValidHash(std::string_view _hash)
{
    auto decoded = base32Decode(_hash);
    return decoded && decoded->size() >= 12 && decoded->size() <= 32;
}

std::vector<std::string_view> split(std::string_view _text, char _separator)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true)
    {
        auto end = _text.find(_separator, start);
        parts.push_back(_text.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return parts;
}
}  // namespace

// ---------------------------------------------------------------------------
// base32
// ---------------------------------------------------------------------------
std::string base32Encode(bytesConstRef _data)
{
    std::string out;
    out.reserve((_data.size() * 8 + 4) / 5);
    uint32_t buffer = 0;
    int bits = 0;
    for (auto byte : _data)
    {
        buffer = (buffer << 8) | byte;
        bits += 8;
        while (bits >= 5)
        {
            bits -= 5;
            out.push_back(c_base32Alphabet[(buffer >> bits) & 0x1f]);
        }
    }
    if (bits > 0)
    {
        out.push_back(c_base32Alphabet[(buffer << (5 - bits)) & 0x1f]);
    }
    return out;
}

std::optional<bcos::bytes> base32Decode(std::string_view _text)
{
    bcos::bytes out;
    out.reserve(_text.size() * 5 / 8);
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : _text)
    {
        int value = -1;
        if (c >= 'A' && c <= 'Z')
        {
            value = c - 'A';
        }
        else if (c >= 'a' && c <= 'z')
        {
            value = c - 'a';
        }
        else if (c >= '2' && c <= '7')
        {
            value = c - '2' + 26;
        }
        else
        {
            return std::nullopt;  // padding is not part of the unpadded form
        }
        buffer = (buffer << 5) | static_cast<uint32_t>(value);
        bits += 5;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<bcos::byte>((buffer >> bits) & 0xff));
        }
    }
    // Leftover bits of an incomplete group are padding; 1, 3 or 6 trailing characters
    // never occur in a canonical encoding.
    auto const rem = _text.size() % 8;
    if (rem == 1 || rem == 3 || rem == 6)
    {
        return std::nullopt;
    }
    return out;
}

// ---------------------------------------------------------------------------
// tree entries
// ---------------------------------------------------------------------------
std::optional<EnrTreeLink> parseEnrTreeUrl(std::string_view _url)
{
    if (_url.substr(0, c_linkPrefix.size()) != c_linkPrefix)
    {
        return std::nullopt;
    }
    auto rest = _url.substr(c_linkPrefix.size());
    auto at = rest.find('@');
    if (at == std::string_view::npos || at == 0 || at + 1 >= rest.size())
    {
        return std::nullopt;
    }
    auto compressed = base32Decode(rest.substr(0, at));
    if (!compressed || compressed->size() != 33)
    {
        return std::nullopt;
    }
    auto publicKey = decompressPublicKey(bcos::ref(*compressed));
    if (!publicKey)
    {
        return std::nullopt;
    }
    return EnrTreeLink{std::string(rest.substr(at + 1)), std::move(*publicKey), std::string(_url)};
}

std::optional<EnrTreeRoot> parseEnrTreeRoot(std::string_view _txt)
{
    // enrtree-root:v1 e=<enr-root> l=<link-root> seq=<n> sig=<sig>
    auto fields = split(_txt, ' ');
    if (fields.size() != 5 || fields[0] != c_rootPrefix || fields[1].substr(0, 2) != "e=" ||
        fields[2].substr(0, 2) != "l=" || fields[3].substr(0, 4) != "seq=" ||
        fields[4].substr(0, 4) != "sig=")
    {
        return std::nullopt;
    }
    EnrTreeRoot root;
    root.enrRoot = std::string(fields[1].substr(2));
    root.linkRoot = std::string(fields[2].substr(2));
    if (!isValidHash(root.enrRoot) || !isValidHash(root.linkRoot))
    {
        return std::nullopt;
    }
    auto seqText = fields[3].substr(4);
    auto [ptr, ec] = std::from_chars(seqText.data(), seqText.data() + seqText.size(), root.seq);
    if (ec != std::errc{} || ptr != seqText.data() + seqText.size())
    {
        return std::nullopt;
    }
    auto signature = base64UrlDecode(fields[4].substr(4));
    if (!signature || signature->size() != 65)
    {
        return std::nullopt;
    }
    root.signature = std::move(*signature);
    return root;
}

bool verifyEnrTreeRoot(EnrTreeRoot const& _root, bytesConstRef _publicKey64)
{
    if (_root.signature.size() != 65)
    {
        return false;
    }
    std::string signed_ = std::string(c_rootPrefix) + " e=" + _root.enrRoot +
                          " l=" + _root.linkRoot + " seq=" + std::to_string(_root.seq);
    auto hash = rlpx::keccak256(
        bytesConstRef(reinterpret_cast<bcos::byte const*>(signed_.data()), signed_.size()));
    // geth verifies r||s only (the recovery id is not checked).
    return verifyCompactSignature(
        _publicKey64, bcos::ref(hash), bytesConstRef(_root.signature.data(), 64));
}

std::string enrTreeEntryHash(std::string_view _content)
{
    auto hash = rlpx::keccak256(
        bytesConstRef(reinterpret_cast<bcos::byte const*>(_content.data()), _content.size()));
    return base32Encode(bytesConstRef(hash.data(), 16));
}

// ---------------------------------------------------------------------------
// libresolv resolver
// ---------------------------------------------------------------------------
#if defined(_WIN32)
std::vector<std::string> ResolvDnsResolver::lookupTxt(std::string const&)
{
    throw std::runtime_error("dnsdisc: libresolv is not available on Windows");
}
#else
std::vector<std::string> ResolvDnsResolver::lookupTxt(std::string const& _name)
{
    struct __res_state state{};
    if (res_ninit(&state) != 0)
    {
        throw std::runtime_error("dnsdisc: res_ninit failed");
    }
    struct Closer
    {
        struct __res_state* state;
        ~Closer() { res_nclose(state); }
    } closer{&state};

    // 65535 is the largest DNS message. res_nquery returns the full message length even
    // when it did not fit, so an oversized length must never reach ns_initparse.
    std::vector<unsigned char> answer(65535);
    int const length =
        res_nquery(&state, _name.c_str(), ns_c_in, ns_t_txt, answer.data(), answer.size());
    if (length < 0)
    {
        if (state.res_h_errno == HOST_NOT_FOUND || state.res_h_errno == NO_DATA)
        {
            return {};
        }
        throw std::runtime_error("dnsdisc: TXT lookup failed for " + _name + " (h_errno " +
                                 std::to_string(state.res_h_errno) + ")");
    }
    if (static_cast<size_t>(length) > answer.size())
    {
        throw std::runtime_error("dnsdisc: truncated DNS answer for " + _name);
    }
    ns_msg message;
    if (ns_initparse(answer.data(), length, &message) != 0)
    {
        throw std::runtime_error("dnsdisc: malformed DNS answer for " + _name);
    }
    std::vector<std::string> records;
    auto const count = ns_msg_count(message, ns_s_an);
    for (int i = 0; i < count; ++i)
    {
        ns_rr rr;
        if (ns_parserr(&message, ns_s_an, i, &rr) != 0 || ns_rr_type(rr) != ns_t_txt)
        {
            continue;
        }
        // RDATA: a sequence of <length><character-string>; one TXT record is their
        // concatenation (as Go's LookupTXT returns it).
        auto const* data = ns_rr_rdata(rr);
        auto remaining = static_cast<size_t>(ns_rr_rdlen(rr));
        std::string text;
        while (remaining > 0)
        {
            size_t const chunk = *data;
            ++data;
            --remaining;
            if (chunk > remaining)
            {
                break;
            }
            text.append(reinterpret_cast<char const*>(data), chunk);
            data += chunk;
            remaining -= chunk;
        }
        records.push_back(std::move(text));
    }
    return records;
}
#endif  // defined(_WIN32)

// ---------------------------------------------------------------------------
// DnsDiscovery
// ---------------------------------------------------------------------------
DnsDiscovery::DnsDiscovery(
    std::vector<std::string> _urls, DnsResolverInterface::Ptr _resolver, RecordSink _sink)
  : m_urls(std::move(_urls)), m_resolver(std::move(_resolver)), m_sink(std::move(_sink))
{}

DnsDiscovery::~DnsDiscovery()
{
    stop();
}

std::vector<Enr> DnsDiscovery::records() const
{
    std::lock_guard lock(m_mutex);
    std::vector<Enr> out;
    out.reserve(m_records.size());
    for (auto const& [id, record] : m_records)
    {
        out.push_back(record);
    }
    return out;
}

DnsDiscovery::SyncReport DnsDiscovery::lastReport() const
{
    std::lock_guard lock(m_mutex);
    return m_lastReport;
}

std::optional<std::string> DnsDiscovery::resolveEntry(
    std::string const& _domain, std::string const& _hash)
{
    {
        std::lock_guard lock(m_mutex);
        if (auto it = m_entryCache.find(_hash); it != m_entryCache.end())
        {
            return it->second;
        }
    }
    std::vector<std::string> txts;
    try
    {
        txts = m_resolver->lookupTxt(_hash + "." + _domain);
    }
    catch (std::exception const& e)
    {
        DNSDISC_LOG(DEBUG) << LOG_DESC("entry lookup failed")
                           << LOG_KV("name", _hash + "." + _domain) << LOG_KV("error", e.what());
        return std::nullopt;
    }
    for (auto& txt : txts)
    {
        if (enrTreeEntryHash(txt) == _hash)
        {
            std::lock_guard lock(m_mutex);
            if (m_entryCache.size() > 10 * c_maxEntriesPerTree)
            {
                m_entryCache.clear();
            }
            m_entryCache.emplace(_hash, txt);
            return std::move(txt);
        }
    }
    DNSDISC_LOG(DEBUG) << LOG_DESC("no entry matching its hash")
                       << LOG_KV("name", _hash + "." + _domain) << LOG_KV("txts", txts.size());
    return std::nullopt;
}

void DnsDiscovery::syncTree(Tree& _tree, SyncReport& _report, std::vector<EnrTreeLink>& _newLinks)
{
    auto const& domain = _tree.link.domain;
    std::vector<std::string> txts;
    try
    {
        txts = m_resolver->lookupTxt(domain);
    }
    catch (std::exception const& e)
    {
        DNSDISC_LOG(WARNING) << LOG_DESC("root lookup failed") << LOG_KV("domain", domain)
                             << LOG_KV("error", e.what());
        ++_report.errors;
        return;
    }
    std::optional<EnrTreeRoot> root;
    for (auto const& txt : txts)
    {
        if ((root = parseEnrTreeRoot(txt)))
        {
            break;
        }
    }
    if (!root)
    {
        DNSDISC_LOG(WARNING) << LOG_DESC("no enrtree-root record") << LOG_KV("domain", domain)
                             << LOG_KV("txts", txts.size());
        ++_report.errors;
        return;
    }
    if (!verifyEnrTreeRoot(*root, bcos::ref(_tree.link.publicKey)))
    {
        DNSDISC_LOG(WARNING) << LOG_DESC("root signature does not verify against the URL key")
                             << LOG_KV("domain", domain) << LOG_KV("seq", root->seq);
        ++_report.errors;
        return;
    }
    _report.lastSeq = std::max(_report.lastSeq, root->seq);
    if (_tree.synced && root->seq <= _tree.seq)
    {
        // Unchanged since the last pass: the linked lists still have to be re-checked.
        _newLinks.insert(_newLinks.end(), _tree.links.begin(), _tree.links.end());
        return;
    }

    // Walk a subtree: branch -> children, leaf -> handler. Hashes already resolved are
    // served from the cache, so a resync after a seq bump only fetches what changed.
    auto walk = [&](std::string const& _rootHash, auto&& _onLeaf) {
        std::vector<std::string> stack{_rootHash};
        std::set<std::string> visited;
        while (!stack.empty() && visited.size() < c_maxEntriesPerTree)
        {
            auto hash = std::move(stack.back());
            stack.pop_back();
            if (!visited.insert(hash).second)
            {
                continue;
            }
            auto content = resolveEntry(domain, hash);
            if (!content)
            {
                continue;
            }
            std::string_view view = *content;
            if (view.substr(0, c_branchPrefix.size()) == c_branchPrefix)
            {
                auto list = view.substr(c_branchPrefix.size());
                if (list.empty())
                {
                    continue;
                }
                for (auto child : split(list, ','))
                {
                    if (isValidHash(child))
                    {
                        stack.emplace_back(child);
                    }
                }
            }
            else
            {
                _onLeaf(view);
            }
        }
    };

    size_t records = 0;
    walk(root->enrRoot, [&](std::string_view _leaf) {
        if (_leaf.substr(0, c_enrPrefix.size()) != c_enrPrefix)
        {
            return;  // links only live under the link root
        }
        auto record = Enr::fromText(_leaf);
        if (!record || !record->verify())
        {
            DNSDISC_LOG(DEBUG) << LOG_DESC("invalid record in tree") << LOG_KV("domain", domain);
            return;
        }
        auto id = record->nodeId();
        if (!id)
        {
            return;
        }
        bool fresh = false;
        {
            std::lock_guard lock(m_mutex);
            auto it = m_records.find(*id);
            if (it == m_records.end() || it->second.seq() < record->seq())
            {
                m_records.insert_or_assign(*id, *record);
                fresh = true;
            }
        }
        if (fresh)
        {
            ++records;
            if (m_sink)
            {
                m_sink(*record);
            }
        }
    });
    std::vector<EnrTreeLink> links;
    walk(root->linkRoot, [&](std::string_view _leaf) {
        if (auto link = parseEnrTreeUrl(_leaf))
        {
            links.push_back(std::move(*link));
        }
    });
    _newLinks.insert(_newLinks.end(), links.begin(), links.end());
    _tree.seq = root->seq;
    _tree.synced = true;
    _tree.links = std::move(links);
    _report.records += records;
    _report.links += _tree.links.size();
    DNSDISC_LOG(INFO) << LOG_DESC("synced node list") << LOG_KV("domain", domain)
                      << LOG_KV("seq", root->seq) << LOG_KV("newRecords", records)
                      << LOG_KV("links", _tree.links.size());
}

DnsDiscovery::SyncReport DnsDiscovery::syncOnce()
{
    SyncReport report;
    std::vector<EnrTreeLink> queue;
    for (auto const& url : m_urls)
    {
        if (auto link = parseEnrTreeUrl(url))
        {
            queue.push_back(std::move(*link));
        }
        else
        {
            DNSDISC_LOG(WARNING) << LOG_DESC("invalid enrtree URL ignored") << LOG_KV("url", url);
            ++report.errors;
        }
    }
    std::set<std::string> visited;
    while (!queue.empty() && report.trees < c_maxTrees)
    {
        auto link = std::move(queue.front());
        queue.erase(queue.begin());
        if (!visited.insert(link.domain).second)
        {
            continue;
        }
        Tree* tree = nullptr;
        {
            std::lock_guard lock(m_mutex);
            auto [it, inserted] = m_trees.try_emplace(link.domain);
            if (inserted || it->second.link.publicKey != link.publicKey)
            {
                it->second = Tree{link, 0, false, {}};
            }
            tree = &it->second;
        }
        ++report.trees;
        syncTree(*tree, report, queue);
    }
    std::lock_guard lock(m_mutex);
    m_lastReport = report;
    return report;
}

void DnsDiscovery::start(std::chrono::seconds _recheckInterval)
{
    {
        std::lock_guard lock(m_runMutex);
        if (m_running)
        {
            return;
        }
        m_running = true;
    }
    m_thread = std::thread([this, _recheckInterval]() {
        while (true)
        {
            try
            {
                syncOnce();
            }
            catch (std::exception const& e)
            {
                DNSDISC_LOG(WARNING) << LOG_DESC("sync failed") << LOG_KV("error", e.what());
            }
            std::unique_lock lock(m_runMutex);
            if (m_wake.wait_for(lock, _recheckInterval, [this]() { return !m_running; }))
            {
                return;
            }
        }
    });
}

void DnsDiscovery::stop()
{
    {
        std::lock_guard lock(m_runMutex);
        m_running = false;
    }
    m_wake.notify_all();
    if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id())
    {
        m_thread.join();
    }
}
}  // namespace bcos::devp2p::disc
