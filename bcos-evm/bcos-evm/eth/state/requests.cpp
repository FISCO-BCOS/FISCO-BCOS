// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2024 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "requests.hpp"
#include <evmone_precompiles/sha256.hpp>

namespace evmone::state
{
namespace
{
consteval uint32_t pad_to_words(uint32_t size) noexcept
{
    return ((size + 31) / 32) * 32;
}
}  // namespace

hash256 calculate_requests_hash(std::span<const Requests> block_requests_list)
{
    bytes requests_hash_list;
    requests_hash_list.reserve(sizeof(hash256) * block_requests_list.size());

    for (const auto& requests : block_requests_list)
    {
        if (requests.data().empty())
            continue;  // Skip empty requests.

        hash256 requests_hash;
        crypto::sha256(reinterpret_cast<std::byte*>(requests_hash.bytes),
            reinterpret_cast<const std::byte*>(requests.raw_data.data()), requests.raw_data.size());
        requests_hash_list += requests_hash;
    }

    hash256 block_requests_hash;
    crypto::sha256(reinterpret_cast<std::byte*>(block_requests_hash.bytes),
        reinterpret_cast<const std::byte*>(requests_hash_list.data()), requests_hash_list.size());
    return block_requests_hash;
}

std::optional<Requests> collect_deposit_requests(std::span<const TransactionReceipt> receipts)
{
    // Browse all logs from all transactions.
    Requests requests(Requests::Type::deposit);
    for (const auto& receipt : receipts)
    {
        for (const auto& log : receipt.logs)
        {
            // Follow the EIP-6110 pseudocode for block validity.
            // https://eips.ethereum.org/EIPS/eip-6110#block-validity

            // Filter out logs by the contact address and the log first topic.
            if (log.addr != DEPOSIT_CONTRACT_ADDRESS)
                continue;
            if (log.topics.empty() || log.topics[0] != DEPOSIT_EVENT_SIGNATURE_HASH)
                continue;

            // Validate the layout of the log. If it doesn't match the EIP spec,
            // the requests' collection is failed.
            if (log.data.size() != 576)
                return std::nullopt;

            // Deposit log definition
            // https://github.com/ethereum/consensus-specs/blob/dev/solidity_deposit_contract/deposit_contract.sol
            // event DepositEvent(
            //     bytes pubkey,
            //     bytes withdrawal_credentials,
            //     bytes amount,
            //     bytes signature,
            //     bytes index
            // );
            //
            // In ABI a word with its size prepends every bytes array.
            // Skip over the first 5 words (offsets of the values) and the pubkey size.
            // Read and validate the ABI offsets and lengths for the dynamic fields
            // according to EIP-6110. If any check fails, collection is considered failed.

            const auto read_word_as_size = [&](size_t pos) -> std::optional<uint32_t> {
                assert(log.data.size() >= pos + 32);
                const auto v = intx::be::unsafe::load<uint256>(&log.data[pos]);
                // Ensure the encoded bytes fit into uint32_t.
                if (v > std::numeric_limits<uint32_t>::max())
                    return std::nullopt;
                return static_cast<uint32_t>(v);
            };

            static constexpr uint32_t WORD = 32;
            assert(log.data.size() >= WORD * 5);

            // Read the 5 offsets from the head (first 5 words).
            std::array<uint32_t, 5> offsets = {};
            for (size_t i = 0; i < offsets.size(); ++i)
            {
                const auto w = read_word_as_size(i * WORD);
                if (!w)
                    return std::nullopt;
                offsets[i] = *w;
            }

            // Compute expected offsets and lengths (hard-coded from the deposit ABI layout).
            static constexpr uint32_t DATA_SECTION =
                WORD * 5;  // where the dynamic data area starts
            static constexpr uint32_t PUBKEY_OFFSET = DATA_SECTION;
            static constexpr uint32_t PUBKEY_SIZE = 48;
            static constexpr uint32_t WITHDRAWAL_OFFSET =
                PUBKEY_OFFSET + WORD + pad_to_words(PUBKEY_SIZE);
            static constexpr uint32_t WITHDRAWAL_SIZE = 32;
            static constexpr uint32_t AMOUNT_OFFSET =
                WITHDRAWAL_OFFSET + WORD + pad_to_words(WITHDRAWAL_SIZE);
            static constexpr uint32_t AMOUNT_SIZE = 8;
            static constexpr uint32_t SIGNATURE_OFFSET =
                AMOUNT_OFFSET + WORD + pad_to_words(AMOUNT_SIZE);
            static constexpr uint32_t SIGNATURE_SIZE = 96;
            static constexpr uint32_t INDEX_OFFSET =
                SIGNATURE_OFFSET + WORD + pad_to_words(SIGNATURE_SIZE);
            static constexpr uint32_t INDEX_SIZE = 8;

            // Offsets in the head point to the length-word of each dynamic field.
            static constexpr std::array EXPECTED_OFFSETS{
                PUBKEY_OFFSET, WITHDRAWAL_OFFSET, AMOUNT_OFFSET, SIGNATURE_OFFSET, INDEX_OFFSET};

            if (offsets != EXPECTED_OFFSETS)
                return std::nullopt;  // layout does not match expected EIP-6110 deposit layout

            // Validate sizes of each field encoded in the log.
            const auto validate_size_at = [&](uint32_t offset, uint32_t expected_size) -> bool {
                const auto size = read_word_as_size(offset);
                return size.has_value() && (*size == expected_size);
            };
            if (!validate_size_at(PUBKEY_OFFSET, PUBKEY_SIZE) ||
                !validate_size_at(WITHDRAWAL_OFFSET, WITHDRAWAL_SIZE) ||
                !validate_size_at(AMOUNT_OFFSET, AMOUNT_SIZE) ||
                !validate_size_at(SIGNATURE_OFFSET, SIGNATURE_SIZE) ||
                !validate_size_at(INDEX_OFFSET, INDEX_SIZE))
            {
                // field size does not match expected EIP-6110 deposit layout
                return std::nullopt;
            }

            // Index is padded to the word boundary, so takes 32 bytes.
            assert(log.data.size() == INDEX_OFFSET + WORD + pad_to_words(INDEX_SIZE));

            requests.append({&log.data[PUBKEY_OFFSET + WORD], PUBKEY_SIZE});
            requests.append({&log.data[WITHDRAWAL_OFFSET + WORD], WITHDRAWAL_SIZE});
            requests.append({&log.data[AMOUNT_OFFSET + WORD], AMOUNT_SIZE});
            requests.append({&log.data[SIGNATURE_OFFSET + WORD], SIGNATURE_SIZE});
            requests.append({&log.data[INDEX_OFFSET + WORD], INDEX_SIZE});
        }
    }
    return requests;
}
}  // namespace evmone::state
