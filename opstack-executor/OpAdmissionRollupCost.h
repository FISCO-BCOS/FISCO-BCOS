// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <opstack-executor/OpEthReceipt.h>
#include <opstack-executor/OpFeeParams.h>
#include <opstack-executor/OpRollupCost.h>
#include <mutex>
#include <optional>

namespace bcos::executor_v1::opstack
{
/// Prices admission from the L1Block attributes of a state-root-pinned view. The caller keeps
/// that view alive and supplies its exact block hash and fork; the cache is keyed by hash so
/// a same-height reorg reloads the slots.
class OpAdmissionRollupCost
{
public:
    template <class View>
    task::Task<u512> price(View& view, crypto::HashType const& headHash, OpForkSpec const& spec,
        bytesConstRef signedEnvelope, uint64_t gasLimit)
    {
        std::optional<OpFeeParams> fee;
        {
            std::lock_guard lock(m_mutex);
            if (m_headHash == headHash)
            {
                fee = m_fee;
            }
        }
        if (!fee)
        {
            fee = co_await loadOpFeeParamsAsync(view);
            std::lock_guard lock(m_mutex);
            m_headHash = headHash;
            m_fee = fee;
        }
        co_return intxToBcosU512(opTotalRollupCost(
            *fee, {signedEnvelope.data(), signedEnvelope.size()}, gasLimit, spec));
    }

private:
    std::mutex m_mutex;
    crypto::HashType m_headHash;
    std::optional<OpFeeParams> m_fee;
};
}  // namespace bcos::executor_v1::opstack
