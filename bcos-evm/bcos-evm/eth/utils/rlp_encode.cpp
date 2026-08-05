// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "rlp_encode.hpp"
#include "rlp.hpp"
#include "stdx/utility.hpp"


namespace evmone::state
{
[[nodiscard]] bytes rlp_encode(const Log& log)
{
    return rlp::encode_tuple(log.addr, log.topics, log.data);
}

[[nodiscard]] bytes rlp_encode(const Transaction& tx)
{
    assert(tx.type <= Transaction::Type::set_code);

    // TODO: Refactor this function. For all type of transactions most of the code is similar.
    if (tx.type == Transaction::Type::legacy)
    {
        // rlp [nonce, gas_price, gas_limit, to, value, data, v, r, s];
        return rlp::encode_tuple(tx.nonce, tx.max_gas_price, static_cast<uint64_t>(tx.gas_limit),
            tx.to.has_value() ? tx.to.value() : bytes_view(), tx.value, tx.data, tx.v, tx.r, tx.s);
    }
    else if (tx.type == Transaction::Type::access_list)
    {
        // tx_type +
        // rlp [chain_id, nonce, gas_price, gas_limit, to, value, data, access_list, v, r, s];
        return bytes{0x01} +  // Transaction type (eip2930 type == 1)
               rlp::encode_tuple(tx.chain_id, tx.nonce, tx.max_gas_price,
                   static_cast<uint64_t>(tx.gas_limit),
                   tx.to.has_value() ? tx.to.value() : bytes_view(), tx.value, tx.data,
                   tx.access_list, tx.v, tx.r, tx.s);
    }
    else if (tx.type == Transaction::Type::eip1559)
    {
        // tx_type +
        // rlp [chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas, gas_limit, to, value,
        // data, access_list, sig_parity, r, s];
        return bytes{0x02} +  // Transaction type (eip1559 type == 2)
               rlp::encode_tuple(tx.chain_id, tx.nonce, tx.max_priority_gas_price, tx.max_gas_price,
                   static_cast<uint64_t>(tx.gas_limit),
                   tx.to.has_value() ? tx.to.value() : bytes_view(), tx.value, tx.data,
                   tx.access_list, tx.v, tx.r, tx.s);
    }
    else if (tx.type == Transaction::Type::blob)
    {
        // tx_type +
        // rlp [chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas, gas_limit, to, value,
        // data, access_list, max_fee_per_blob_gas, blob_versioned_hashes, sig_parity, r, s];
        return bytes{stdx::to_underlying(Transaction::Type::blob)} +
               rlp::encode_tuple(tx.chain_id, tx.nonce, tx.max_priority_gas_price, tx.max_gas_price,
                   static_cast<uint64_t>(tx.gas_limit),
                   tx.to.has_value() ? tx.to.value() : bytes_view(), tx.value, tx.data,
                   tx.access_list, tx.max_blob_gas_price, tx.blob_hashes, tx.v, tx.r, tx.s);
    }
    else
    {
        assert(tx.type == Transaction::Type::set_code);
        // tx_type +
        // rlp [chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas, gas_limit, to, value,
        // data, access_list, authorization_list, sig_parity, r, s];
        return bytes{0x04} +  // Transaction type (set_code type == 4)
               rlp::encode_tuple(tx.chain_id, tx.nonce, tx.max_priority_gas_price, tx.max_gas_price,
                   static_cast<uint64_t>(tx.gas_limit),
                   tx.to.has_value() ? tx.to.value() : bytes_view(), tx.value, tx.data,
                   tx.access_list, tx.authorization_list, tx.v, tx.r, tx.s);
    }
}

[[nodiscard]] bytes rlp_encode(const TransactionReceipt& receipt)
{
    if (receipt.post_state.has_value())
    {
        assert(receipt.type == Transaction::Type::legacy);

        return rlp::encode_tuple(receipt.post_state.value(),
            static_cast<uint64_t>(receipt.cumulative_gas_used),
            bytes_view(receipt.logs_bloom_filter), receipt.logs);
    }
    else
    {
        const auto prefix = receipt.type == Transaction::Type::legacy ?
                                bytes{} :
                                bytes{stdx::to_underlying(receipt.type)};

        return prefix + rlp::encode_tuple(receipt.status == EVMC_SUCCESS,
                            static_cast<uint64_t>(receipt.cumulative_gas_used),
                            bytes_view(receipt.logs_bloom_filter), receipt.logs);
    }
}

[[nodiscard]] bytes rlp_encode(const Authorization& authorization)
{
    return rlp::encode_tuple(authorization.chain_id, authorization.addr, authorization.nonce,
        authorization.v, authorization.r, authorization.s);
}

[[nodiscard]] bytes rlp_encode(const Withdrawal& withdrawal)
{
    return rlp::encode_tuple(withdrawal.index, withdrawal.validator_index, withdrawal.recipient,
        withdrawal.amount_in_gwei);
}
}  // namespace evmone::state
