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
 * @file HeaderChain.h
 * @brief Header download with continuity + parentHash chain validation
 *        (port of silkworm's sync header chain, simplified for eth/68 sync).
 * @date 2026/8/18
 */
#pragma once

#include "Block.h"
#include "HeaderValidator.h"
#include "../rlpx/Messages.h"
#include "../rlpx/Session.h"
#include "../eth/Protocol.h"
#include <functional>
#include <stdexcept>

namespace bcos::devp2p::sync
{
// A validated header together with its keccak hash and raw wire encoding.
struct HeaderWithHash
{
    bcos::protocol::EthBlockHeaderData header;
    bcos::h256 hash;
    bcos::bytes rlp;

    uint64_t number() const { return static_cast<uint64_t>(header.number); }
    bcos::h256 parentHash() const { return header.parentInfo.blockHash; }
};

// The first header of a peer's batch does not build on the local anchor — a
// fork/reorg signal, typed so the sync loop classifies it by failure origin
// instead of string-matching the message.
struct ParentHashMismatch : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// A downloaded header violates the Ethereum PoS field rules against its parent.
// Deterministic: every honest peer's copy of the same block fails identically.
struct HeaderRuleViolation : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Downloads a contiguous run of headers from a peer, validating the ascending
// block-number sequence, the parentHash chain against a local anchor, and —
// when the anchor header is known — the per-header Ethereum PoS field rules
// (EIP-1559 base fee, EIP-4844 blob gas, gas-limit bounds, ...).
class HeaderChain
{
public:
    HeaderChain(uint64_t _nextNumber, bcos::h256 _anchorHash,
        uint64_t _maxHeadersPerRequest = 192);

    // With a known anchor header (the local block before nextNumber), every
    // downloaded header is additionally validated against Ethereum PoS rules.
    HeaderChain(uint64_t _nextNumber, bcos::protocol::EthBlockHeaderData _anchorHeader,
        ChainConfig const& _config = {}, uint64_t _maxHeadersPerRequest = 192);

    uint64_t nextNumber() const { return m_nextNumber; }
    bcos::h256 anchorHash() const { return m_anchorHash; }

    // Request up to `_amount` headers starting at nextNumber. Validates
    // contiguity, the parent chain and (when the anchor header is known) the
    // PoS field rules. Throws on any protocol violation.
    std::vector<HeaderWithHash> requestHeaders(rlpx::Session& _session, uint64_t _amount);

    // Request a SINGLE header by hash (eth/68 originHash) — used to resolve the
    // peer's announced head hash to a block number before a bounded download.
    // Does not move the download anchor or validate against it. Returns nullopt
    // when the peer does not know the hash.
    std::optional<HeaderWithHash> requestHeaderByHash(
        rlpx::Session& _session, bcos::h256 const& _hash);

    // Per-header validation policy: (header, parent) -> result. Defaults to the
    // Ethereum PoS rules (validateHeaderPoS over m_config); OP Stack sync injects
    // makeOpHeaderValidator (sync/OpHeaderValidator.h) instead. Only consulted when
    // the anchor header is known; the typed exception semantics of requestHeaders
    // (HeaderRuleViolation on a rule failure) are unchanged.
    using HeaderValidatorFn = std::function<HeaderValidationResult(
        bcos::protocol::EthBlockHeaderData const&, bcos::protocol::EthBlockHeaderData const&)>;
    void setHeaderValidator(HeaderValidatorFn _validator) { m_validator = std::move(_validator); }

    // Advance the anchor past `_count` downloaded headers.
    void advance(uint64_t _count, HeaderWithHash const& _lastHeader);

private:
    uint64_t m_nextNumber;
    bcos::h256 m_anchorHash;
    std::optional<bcos::protocol::EthBlockHeaderData> m_anchorHeader;
    ChainConfig m_config;
    HeaderValidatorFn m_validator;
    uint64_t m_maxHeadersPerRequest;
    uint64_t m_requestId{0};
};
}  // namespace bcos::devp2p::sync
