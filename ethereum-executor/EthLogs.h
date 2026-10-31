/// @file EthLogs.h
/// @brief Shared conversion from the host's accumulated evmc logs to the BCOS
///        protocol::LogEntry vector. Used by every receipt builder
///        (EthL1Policy::buildReceipt, OpPolicy::buildReceipt, opRunDeposit) —
///        the loop was previously copied verbatim into each of them.

#pragma once

#include "bcos-framework/protocol/LogEntry.h"
#include <evmc/evmc.h>
#include <vector>

namespace bcos::executor_v1::eth
{
/// Drains the host's logs and converts each evmc log entry to a
/// protocol::LogEntry. Templated on the host type so it works for any
/// policy's host instantiation — only take_logs() is consumed.
template <class Host>
std::vector<protocol::LogEntry> takeBcosLogs(Host& host)
{
    std::vector<protocol::LogEntry> logs;
    for (auto const& l : host.take_logs())
    {
        bcos::bytes addr(l.addr.bytes, l.addr.bytes + sizeof(evmc_address));
        bcos::h256s topics;
        for (auto const& t : l.topics)
            topics.emplace_back(bcos::bytesConstRef(t.bytes, sizeof(evmc_bytes32)));
        bcos::bytes data(l.data.begin(), l.data.end());
        logs.emplace_back(std::move(addr), std::move(topics), std::move(data));
    }
    return logs;
}
}  // namespace bcos::executor_v1::eth
