#pragma once

// Standard ERC-20 contract used by both executor benchmarks
// (transaction-executor/benchmark and ethereum-executor/benchmark) so the two
// executors are measured on identical bytecode and identical workloads.
//
// Source: transaction-executor/benchmark/BenchmarkERC20.sol
// Compiler: solc 0.8.26, evm_version=shanghai (PUSH0, no MCOPY/TSTORE so both
// executors run the same opcodes), optimizer 200 runs. Creation bytecode
// below (constructor mints 1e9 * 1e18 to the deployer).

#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include <cstdint>
#include <string_view>
#include <fmt/format.h>

namespace bcos::benchmark_erc20
{
constexpr static std::string_view erc20Bytecode =
    "6080604052348015600e575f80fd5b506023336b033b2e3c9fd0803ce80000006027565b60cc565b805f80828254603591"
    "9060a8565b90915550506001600160a01b0382165f9081526001602052604081208054839290605f90849060a8565b909155"
    "50506040518181526001600160a01b038316905f907fddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4d"
    "f523b3ef9060200160405180910390a35050565b8082018082111560c657634e487b7160e01b5f52601160045260245ffd5b"
    "92915050565b61059e806100d95f395ff3fe608060405234801561000f575f80fd5b5060043610610090575f3560e01c8063"
    "313ce56711610063578063313ce5671461012357806370a082311461013d57806395d89b411461015c578063a9059cbb1461"
    "0180578063dd62ed3e14610193575f80fd5b806306fdde0314610094578063095ea7b3146100d757806318160ddd146100fa"
    "57806323b872dd14610110575b5f80fd5b6100c16040518060400160405280600e81526020016d2132b731b436b0b935aa37"
    "b5b2b760911b81525081565b6040516100ce9190610415565b60405180910390f35b6100ea6100e536600461047b565b6101"
    "bd565b60405190151581526020016100ce565b6101025f5481565b6040519081526020016100ce565b6100ea61011e366004"
    "6104a3565b610229565b61012b601281565b60405160ff90911681526020016100ce565b61010261014b3660046104dd565b"
    "60016020525f908152604090205481565b6100c1604051806040016040528060058152602001640848a9c86960db1b815250"
    "81565b6100ea61018e36600461047b565b6102ed565b6101026101a13660046104fd565b600260209081525f928352604080"
    "842090915290825290205481565b335f8181526002602090815260408083206001600160a01b038716808552925280832085"
    "905551919290917f8c5be1e5ebec7d5bd14f71427d1e84f3dd0314c0f7b2291e5b200ac8c7c3b92590610217908681526020"
    "0190565b60405180910390a35060015b92915050565b6001600160a01b0383165f9081526002602090815260408083203384"
    "52909152812054828110156102a15760405162461bcd60e51b815260206004820152601d60248201527f45524332303a2069"
    "6e73756666696369656e7420616c6c6f77616e636500000060448201526064015b60405180910390fd5b5f1981146102d757"
    "6102b38382610542565b6001600160a01b0386165f9081526002602090815260408083203384529091529020555b6102e285"
    "8585610302565b506001949350505050565b5f6102f9338484610302565b50600192915050565b6001600160a01b0383165f"
    "908152600160205260409020548111156103695760405162461bcd60e51b815260206004820152601b60248201527f455243"
    "32303a20696e73756666696369656e742062616c616e636500000000006044820152606401610298565b6001600160a01b03"
    "83165f9081526001602052604081208054839290610390908490610542565b90915550506001600160a01b0382165f908152"
    "60016020526040812080548392906103bc908490610555565b92505081905550816001600160a01b0316836001600160a01b"
    "03167fddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef836040516104089181526020019056"
    "5b60405180910390a3505050565b602081525f82518060208401525f5b818110156104415760208186018101516040868401"
    "015201610424565b505f604082850101526040601f19601f83011684010191505092915050565b80356001600160a01b0381"
    "168114610476575f80fd5b919050565b5f806040838503121561048c575f80fd5b61049583610460565b9460209390930135"
    "93505050565b5f805f606084860312156104b5575f80fd5b6104be84610460565b92506104cc60208501610460565b929592"
    "945050506040919091013590565b5f602082840312156104ed575f80fd5b6104f682610460565b9392505050565b5f806040"
    "838503121561050e575f80fd5b61051783610460565b915061052560208401610460565b90509250929050565b634e487b71"
    "60e01b5f52601160045260245ffd5b818103818111156102235761022361052e565b80820180821115610223576102236105"
    "2e56fea2646970667358221220eadcc9724ba792a5ac0a80db341117fc1b0033f051d372367cfd4392764d5e2864736f6c63"
    "4300081a0033"    ;

// Number of distinct user accounts driving the approve/transferFrom workloads.
constexpr static uint64_t USER_COUNT = 1000;

// Account layout shared by both executor benchmarks:
//   index 0               -> deployer (deploys the token, funds every user)
//   index 1..USER_COUNT   -> the users (approve / transferFrom sources)
//   index USER_COUNT + 1  -> spender (sends every transferFrom)
//   index USER_COUNT + 2  -> receiver (transferFrom destination)
constexpr static uint64_t DEPLOYER_INDEX = 0;
constexpr static uint64_t SPENDER_INDEX = USER_COUNT + 1;
constexpr static uint64_t RECEIVER_INDEX = USER_COUNT + 2;

// Tokens minted to the deployer by the constructor: 1e9 * 1e18.
// Each user is funded with USER_INITIAL_BALANCE so transferFrom never runs dry.
constexpr static uint64_t USER_INITIAL_BALANCE = 1000000000000;  // 1e12
// transferFrom moves 1 wei per transaction, and each user approves the spender
// for 1e18, so balance/allowance outlive any benchmark run.
constexpr static uint64_t TRANSFER_FROM_AMOUNT = 1;

// Gas limit attached to every benchmark transaction. approve / transfer /
// transferFrom stay well below it; both executors validate intrinsic gas
// against this value.
constexpr static int64_t TX_GAS_LIMIT = 500000;

// Deterministic address for account index i (big-endian i right-aligned in 20
// bytes). The benchmarks derive every sender/recipient from this so both
// executors touch exactly the same account set.
inline bcos::Address userAddress(uint64_t index)
{
    return bcos::Address(static_cast<unsigned>(index));
}

// Build one legacy (kind 0, gasPrice 0) transaction carrying an ABI-encoded
// ERC-20 call. Both executors consume the exact same transaction shape; the
// explicit sender + nonce matter for the ethereum-executor, which enforces
// strict per-sender nonce ordering and EIP-3607 — the transaction-executor
// simply ignores them. An empty `to` means contract creation.
inline bcostars::protocol::TransactionImpl makeTransaction(bcos::bytesConstRef input,
    std::string to, bcos::Address const& sender, uint64_t nonce)
{
    bcostars::protocol::TransactionImpl transaction(
        [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
    auto& inner = transaction.mutableInner();
    inner.data.input.assign(input.begin(), input.end());
    inner.data.to = std::move(to);
    inner.data.nonce = fmt::format("{:#x}", nonce);
    inner.data.value = "0x0";
    inner.data.gasLimit = TX_GAS_LIMIT;
    inner.dataHash.resize(1);
    inner.sender.assign(sender.begin(), sender.end());
    return transaction;
}
}  // namespace bcos::benchmark_erc20
