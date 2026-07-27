// bcos-evm-ref/include/bcos-evm-ref/adapter/StateViewAdapter.h
#pragma once

#include <bcos-evm/eth/state/state_view.hpp>

namespace bcos::evm
{
/// v1 placeholder (spec §3.1/§4.1, BlockHashesAdapter is merged in here, see the "declaration
/// deviation" section): the test backend uses evmone::test::TestState directly.
/// When bridging a real ledger, implement evmone::state::StateView's three read-only methods here;
/// note that StateView is a synchronous noexcept interface and get_account_code returns the entire
/// code by value — the performance assessment of bridging a coroutine-based ledger is in
/// spec §7.2 (M3.5 spike, go/no-go).
using StateView = evmone::state::StateView;
using BlockHashes = evmone::state::BlockHashes;
}  // namespace bcos::evm
