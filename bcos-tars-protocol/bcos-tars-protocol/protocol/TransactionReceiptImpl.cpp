/**
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
 * @brief tars implementation for TransactionReceipt
 * @file TransactionReceiptImpl.cpp
 * @author: ancelmo
 * @date 2021-04-20
 */
#include "TransactionReceiptImpl.h"
#include "../impl/TarsHashable.h"
#include "../impl/TarsSerializable.h"
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-utilities/BoostLog.h>
#include <algorithm>
#include <cassert>
#include <charconv>
#include <iterator>
#include <limits>
#include <system_error>

DERIVE_BCOS_EXCEPTION(EmptyReceiptHash);

#define TRANSACTIONRECEIPT_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TRANSACTION_RECEIPT")

namespace
{
// Local hex helpers for the opStackMeta tars fields. All 13 fields are hex strings so that
// explicit zeros ("0x0") survive tars serialization (tars optional scalars have no presence
// semantics). u256 formats via number::str(digits, base); the u64 path uses std::to_chars to
// avoid constructing a big-integer intermediate on every getter.

std::string u256ToHex(bcos::u256 const& v)
{
    // 0 encodes as "0x0" (non-empty), preserving field presence
    return "0x" + v.str(0, std::ios_base::hex);
}
std::string u64ToHex(uint64_t v)
{
    // Hand-written hex: avoids constructing boost::multiprecision::uint256_t (a big-integer
    // intermediate conversion on every getter across the 10 u64 fields). 0 encodes as "0x0"
    // (non-empty, preserving field presence) — consistent with u256ToHex. std::to_chars is
    // allocation-free; note it does NOT NUL-terminate (we construct the string from the
    // returned length ptr - buf, not from a strlen), unlike a hand-rolled buffer would.
    if (v == 0)
    {
        return "0x0";
    }
    char buf[2 + 16 + 1];
    auto const res = std::to_chars(buf + 2, std::end(buf), v, 16);
    // `ec` is only read by the assert below, which compiles out under NDEBUG; [[maybe_unused]]
    // keeps that intent explicit in the language instead of a bare (void)ec cast. (Applied to a
    // plain variable, not a structured binding, because Clang rejects attributes on the
    // identifier-list of a structured binding declaration.)
    [[maybe_unused]] auto const& ec = res.ec;
    // buf is 19 bytes; uint64_t max needs 16 hex digits, so to_chars cannot fail here.
    // This assert documents that invariant — it is not a guard (it vanishes under NDEBUG).
    assert(ec == std::errc{});
    buf[0] = '0';
    buf[1] = 'x';
    return std::string(buf, static_cast<std::size_t>(res.ptr - buf));
}

/// True when opStackMeta is entirely empty (a legacy receipt never wrote field 8). A tars
/// optional string uses != "" to mean "present" (0 values are stored "0x0", non-empty), so
/// all-empty means legacy receipt.
bool opStackMetaEmpty(bcostars::OpStackReceiptMeta const& s)
{
    return s.l1_gas_price.empty() && s.l1_fee.empty() && s.l1_blob_base_fee.empty() &&
           s.l1_base_fee_scalar.empty() && s.l1_blob_base_fee_scalar.empty() &&
           s.operator_fee_scalar.empty() && s.operator_fee_constant.empty() &&
           s.da_footprint_gas_scalar.empty() && s.da_footprint.empty() && s.deposit_nonce.empty() &&
           s.deposit_receipt_version.empty() && s.l1_gas_used.empty() && s.operator_fee.empty();
}
std::optional<bcos::u256> hexToU256(std::string const& s)
{
    if (s.empty())
    {
        return std::nullopt;
    }
    // Use safeFromHex (internal try/catch): invalid hex from corrupt data (bit rot / external
    // writes) returns nullopt instead of throwing BadHexCharacter through the const getter,
    // avoiding a crash when RPC queries the receipt.
    auto bytes = bcos::safeFromHex(s);
    if (!bytes || bytes->empty())
    {
        return std::nullopt;  // bare "0x" (empty payload): missing, not present-with-0
    }
    // fromBigEndian<u256> silently truncates inputs wider than 32 bytes (high bytes shifted out).
    // Strip leading 0x00 first (canonical), then reject if any nonzero byte remains beyond 32 —
    // a corrupt oversized value must not be silently folded into the low 256 bits.
    auto it = std::find_if(bytes->begin(), bytes->end(), [](bcos::byte b) { return b != 0; });
    std::size_t const significant = bytes->end() - it;
    if (significant > 32)
    {
        return std::nullopt;
    }
    return bcos::fromBigEndian<bcos::u256>(*bytes);
}
std::optional<uint64_t> hexToU64(std::string const& s)
{
    if (s.empty())
    {
        return std::nullopt;
    }
    // Same as above: safeFromHex guards against corrupt input.
    auto bytes = bcos::safeFromHex(s);
    if (!bytes || bytes->empty())
    {
        return std::nullopt;  // bare "0x" (empty payload): missing, not present-with-0
    }
    // fromBigEndian<uint64_t> silently truncates inputs wider than 8 bytes. Same guard as
    // hexToU256: strip leading zeros, reject nonzero bytes beyond 8.
    auto it = std::find_if(bytes->begin(), bytes->end(), [](bcos::byte b) { return b != 0; });
    std::size_t const significant = bytes->end() - it;
    if (significant > 8)
    {
        return std::nullopt;
    }
    return bcos::fromBigEndian<uint64_t>(*bytes);
}
std::optional<uint32_t> hexToU32(std::string const& s)
{
    // The three scalar fields (l1_base_fee_scalar / l1_blob_base_fee_scalar / operator_fee_scalar)
    // are uint32_t in the producer (OpReceiptMeta). A stored value wider than 4 bytes is corrupt
    // (or externally written); reject rather than narrow silently — the widened read-back is the
    // exact hazard the width alignment prevents.
    auto value = hexToU64(s);
    if (!value || *value > std::numeric_limits<uint32_t>::max())
    {
        return std::nullopt;
    }
    return static_cast<uint32_t>(*value);
}
}  // namespace

bcostars::protocol::TransactionReceiptImpl::TransactionReceiptImpl()
  : m_inner([m_receipt = bcostars::TransactionReceipt()]() mutable {
        return std::addressof(m_receipt);
    })
{}

void bcostars::protocol::TransactionReceiptImpl::decode(bcos::bytesConstRef _receiptData)
{
    bcos::concepts::serialize::decode(_receiptData, *m_inner());
}

void bcostars::protocol::TransactionReceiptImpl::encode(bcos::bytes& _encodedData) const
{
    bcos::concepts::serialize::encode(*m_inner(), _encodedData);
}

bcos::crypto::HashType bcostars::protocol::TransactionReceiptImpl::hash() const
{
    if (m_inner()->dataHash.empty())
    {
        throwTrace(EmptyReceiptHash{});
    }

    bcos::crypto::HashType hashResult(
        (bcos::byte*)m_inner()->dataHash.data(), m_inner()->dataHash.size());

    return hashResult;
}
void bcostars::protocol::TransactionReceiptImpl::calculateHash(const bcos::crypto::Hash& hashImpl)
{
    bcos::concepts::hash::calculate(*m_inner(), hashImpl.hasher(), m_inner()->dataHash);
}

bcos::u256 bcostars::protocol::TransactionReceiptImpl::gasUsed() const
{
    if (!m_inner()->data.gasUsed.empty())
    {
        return boost::lexical_cast<bcos::u256>(m_inner()->data.gasUsed);
    }
    return {};
}
int32_t bcostars::protocol::TransactionReceiptImpl::version() const
{
    return m_inner()->data.version;
}
std::string_view bcostars::protocol::TransactionReceiptImpl::contractAddress() const
{
    return m_inner()->data.contractAddress;
}
int32_t bcostars::protocol::TransactionReceiptImpl::status() const
{
    return m_inner()->data.status;
}
bcos::bytesConstRef bcostars::protocol::TransactionReceiptImpl::output() const
{
    return {(const unsigned char*)m_inner()->data.output.data(), m_inner()->data.output.size()};
}
gsl::span<const bcos::protocol::LogEntry> bcostars::protocol::TransactionReceiptImpl::logEntries()
    const
{
    if (m_logEntries.empty())
    {
        m_logEntries.reserve(m_inner()->data.logEntries.size());
        for (auto& it : m_inner()->data.logEntries)
        {
            auto bcosLogEntry = toBcosLogEntry(it);
            m_logEntries.emplace_back(std::move(bcosLogEntry));
        }
    }

    return {m_logEntries.data(), m_logEntries.size()};
}
bcos::protocol::LogEntries bcostars::protocol::TransactionReceiptImpl::takeLogEntries()
{
    if (m_logEntries.empty())
    {
        auto& data = inner();
        m_logEntries.reserve(data.data.logEntries.size());
        for (auto& it : data.data.logEntries)
        {
            auto bcosLogEntry = takeToBcosLogEntry(std::move(it));
            m_logEntries.push_back(std::move(bcosLogEntry));
        }
        return std::move(m_logEntries);
    }
    return std::move(m_logEntries);
}
bcos::protocol::BlockNumber bcostars::protocol::TransactionReceiptImpl::blockNumber() const
{
    return m_inner()->data.blockNumber;
}
std::string_view bcostars::protocol::TransactionReceiptImpl::effectiveGasPrice() const
{
    return m_inner()->data.effectiveGasPrice;
}
void bcostars::protocol::TransactionReceiptImpl::setEffectiveGasPrice(std::string effectiveGasPrice)
{
    m_inner()->data.effectiveGasPrice = std::move(effectiveGasPrice);
}
std::optional<bcos::protocol::OpStackReceiptMeta>
bcostars::protocol::TransactionReceiptImpl::opStackMeta() const
{
    auto const& s = m_inner()->opStackMeta;
    // A legacy receipt (field 8 never set) decodes to an all-empty opStackMeta: short-circuit
    // here to avoid building the whole struct.
    if (opStackMetaEmpty(s))
    {
        return std::nullopt;
    }
    bcos::protocol::OpStackReceiptMeta out;
    // all 13 fields are hex strings; a tars optional string uses != "" to mean "present"
    // (0 values are stored "0x0", non-empty, so explicit zeros keep their presence)
    if (!s.l1_gas_price.empty())
        out.l1_gas_price = hexToU256(s.l1_gas_price);
    if (!s.l1_fee.empty())
        out.l1_fee = hexToU256(s.l1_fee);
    if (!s.l1_blob_base_fee.empty())
        out.l1_blob_base_fee = hexToU256(s.l1_blob_base_fee);
    if (!s.l1_base_fee_scalar.empty())
        out.l1_base_fee_scalar = hexToU32(s.l1_base_fee_scalar);
    if (!s.l1_blob_base_fee_scalar.empty())
        out.l1_blob_base_fee_scalar = hexToU32(s.l1_blob_base_fee_scalar);
    if (!s.operator_fee_scalar.empty())
        out.operator_fee_scalar = hexToU32(s.operator_fee_scalar);
    if (!s.operator_fee_constant.empty())
        out.operator_fee_constant = hexToU64(s.operator_fee_constant);
    if (!s.da_footprint_gas_scalar.empty())
        out.da_footprint_gas_scalar = hexToU64(s.da_footprint_gas_scalar);
    if (!s.da_footprint.empty())
        out.da_footprint = hexToU64(s.da_footprint);
    if (!s.deposit_nonce.empty())
        out.deposit_nonce = hexToU64(s.deposit_nonce);
    if (!s.deposit_receipt_version.empty())
        out.deposit_receipt_version = hexToU64(s.deposit_receipt_version);
    if (!s.l1_gas_used.empty())
        out.l1_gas_used = hexToU64(s.l1_gas_used);
    if (!s.operator_fee.empty())
        out.operator_fee = hexToU256(s.operator_fee);
    // Reached only when every field that was WRITTEN failed to parse (corrupt hex, oversized, or
    // a bare "0x"). The legacy all-empty case already returned above via opStackMetaEmpty(s).
    // Report nullopt rather than an OP receipt with 13 absent fields, and log so the corruption
    // is not invisible.
    if (out.l1_gas_price == std::nullopt && out.l1_fee == std::nullopt &&
        out.l1_blob_base_fee == std::nullopt && out.l1_base_fee_scalar == std::nullopt &&
        out.l1_blob_base_fee_scalar == std::nullopt && out.operator_fee_scalar == std::nullopt &&
        out.operator_fee_constant == std::nullopt && out.da_footprint_gas_scalar == std::nullopt &&
        out.da_footprint == std::nullopt && out.deposit_nonce == std::nullopt &&
        out.deposit_receipt_version == std::nullopt && out.l1_gas_used == std::nullopt &&
        out.operator_fee == std::nullopt)
    {
        TRANSACTIONRECEIPT_LOG(WARNING) << LOG_DESC("opStackMeta present but entirely unparseable");
        return std::nullopt;
    }
    return out;
}
void bcostars::protocol::TransactionReceiptImpl::setOpStackMeta(
    bcos::protocol::OpStackReceiptMeta const& meta)
{
    // Semantics are "replace", not "merge": clear first so a second call cannot leave stale
    // non-empty fields from a previous invocation (a review noted that merge semantics would
    // retain old values when setOpStackMeta is called twice on the same receipt).
    m_inner()->opStackMeta = {};
    auto& s = m_inner()->opStackMeta;
    if (meta.l1_gas_price)
        s.l1_gas_price = u256ToHex(*meta.l1_gas_price);
    if (meta.l1_fee)
        s.l1_fee = u256ToHex(*meta.l1_fee);
    if (meta.l1_blob_base_fee)
        s.l1_blob_base_fee = u256ToHex(*meta.l1_blob_base_fee);
    if (meta.l1_base_fee_scalar)
        s.l1_base_fee_scalar = u64ToHex(*meta.l1_base_fee_scalar);
    if (meta.l1_blob_base_fee_scalar)
        s.l1_blob_base_fee_scalar = u64ToHex(*meta.l1_blob_base_fee_scalar);
    if (meta.operator_fee_scalar)
        s.operator_fee_scalar = u64ToHex(*meta.operator_fee_scalar);
    if (meta.operator_fee_constant)
        s.operator_fee_constant = u64ToHex(*meta.operator_fee_constant);
    if (meta.da_footprint_gas_scalar)
        s.da_footprint_gas_scalar = u64ToHex(*meta.da_footprint_gas_scalar);
    if (meta.da_footprint)
        s.da_footprint = u64ToHex(*meta.da_footprint);
    if (meta.deposit_nonce)
        s.deposit_nonce = u64ToHex(*meta.deposit_nonce);
    if (meta.deposit_receipt_version)
        s.deposit_receipt_version = u64ToHex(*meta.deposit_receipt_version);
    if (meta.l1_gas_used)
        s.l1_gas_used = u64ToHex(*meta.l1_gas_used);
    if (meta.operator_fee)
        s.operator_fee = u256ToHex(*meta.operator_fee);
}
const bcostars::TransactionReceipt& bcostars::protocol::TransactionReceiptImpl::inner() const
{
    return *m_inner();
}
bcostars::TransactionReceipt& bcostars::protocol::TransactionReceiptImpl::inner()
{
    return *m_inner();
}
void bcostars::protocol::TransactionReceiptImpl::setInner(const bcostars::TransactionReceipt& inner)
{
    *m_inner() = inner;
}
void bcostars::protocol::TransactionReceiptImpl::setInner(bcostars::TransactionReceipt&& inner)
{
    *m_inner() = std::move(inner);
}
std::function<bcostars::TransactionReceipt*()> const&
bcostars::protocol::TransactionReceiptImpl::innerGetter()
{
    return m_inner;
}
void bcostars::protocol::TransactionReceiptImpl::setLogEntries(
    std::vector<bcos::protocol::LogEntry> const& _logEntries)
{
    m_logEntries.clear();
    m_inner()->data.logEntries.clear();
    m_inner()->data.logEntries.reserve(_logEntries.size());

    for (const auto& it : _logEntries)
    {
        auto tarsLogEntry = toTarsLogEntry(it);
        m_inner()->data.logEntries.emplace_back(std::move(tarsLogEntry));
    }
}
std::string const& bcostars::protocol::TransactionReceiptImpl::message() const
{
    return m_inner()->message;
}
void bcostars::protocol::TransactionReceiptImpl::setMessage(std::string message)
{
    m_inner()->message = std::move(message);
}
size_t bcostars::protocol::TransactionReceiptImpl::size() const
{
    size_t size = 0;
    size += m_inner()->data.output.size();
    for (auto& it : m_inner()->data.logEntries)
    {
        size += it.data.size();
        size += it.address.size();
        for (auto& topic : it.topic)
        {
            size += topic.size();
        }
    }
    size += m_inner()->message.size();
    // opStackMeta: count raw string payloads (like output/message above), NOT the
    // tars-encoded byte size. Each field is an optional string; a field present but
    // empty ("" = absent) contributes 0, matching tars optional semantics.
    // Short-circuit: 99% of receipts carry no OP metadata.
    if (!opStackMetaEmpty(m_inner()->opStackMeta))
    {
        auto const& s = m_inner()->opStackMeta;
        size += s.l1_gas_price.size();
        size += s.l1_fee.size();
        size += s.l1_blob_base_fee.size();
        size += s.l1_base_fee_scalar.size();
        size += s.l1_blob_base_fee_scalar.size();
        size += s.operator_fee_scalar.size();
        size += s.operator_fee_constant.size();
        size += s.da_footprint_gas_scalar.size();
        size += s.da_footprint.size();
        size += s.deposit_nonce.size();
        size += s.deposit_receipt_version.size();
        size += s.l1_gas_used.size();
        size += s.operator_fee.size();
    }
    return size;
}

bcostars::protocol::TransactionReceiptImpl::TransactionReceiptImpl(
    std::function<bcostars::TransactionReceipt*()> inner)
  : m_inner(std::move(inner))
{}

size_t bcostars::protocol::TransactionReceiptImpl::transactionIndex() const
{
    return m_inner()->transactionIndex;
}
void bcostars::protocol::TransactionReceiptImpl::setTransactionIndex(size_t index)
{
    m_inner()->transactionIndex = index;
}
std::string_view bcostars::protocol::TransactionReceiptImpl::cumulativeGasUsed() const
{
    return m_inner()->cumulativeGasUsed;
}
void bcostars::protocol::TransactionReceiptImpl::setCumulativeGasUsed(std::string cumulativeGasUsed)
{
    m_inner()->cumulativeGasUsed = std::move(cumulativeGasUsed);
}
bcos::bytesConstRef bcostars::protocol::TransactionReceiptImpl::logsBloom() const
{
    auto* inner = m_inner();
    return {(bcos::byte*)inner->logsBloom.data(), inner->logsBloom.size()};
}
void bcostars::protocol::TransactionReceiptImpl::setLogsBloom(bcos::bytesConstRef logsBloom)
{
    m_inner()->logsBloom.assign(logsBloom.data(), logsBloom.data() + logsBloom.size());
}
size_t bcostars::protocol::TransactionReceiptImpl::logIndex() const
{
    return 0;
}
void bcostars::protocol::TransactionReceiptImpl::setLogIndex(size_t index) {}
