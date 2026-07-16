// bcos-evm-ref/include/bcos-evm-ref/adapter/StateViewAdapter.h
#pragma once

#include <test/state/state_view.hpp>

namespace bcos::evmref
{
/// v1 placeholder (spec §3.1/§4.1, BlockHashesAdapter is merged in here, see the "declaration
/// deviation" section):
/// the test backend uses evmone::test::TestState directly.
/// When bridging a real ledger, implement the three read-only methods of evmone::state::StateView
/// here;
/// note that StateView is a synchronous noexcept interface and get_account_code returns the entire
/// code by value,
/// see spec §7.2 for the performance evaluation of bridging a coroutine-based ledger (M3.5 spike,
/// go/no-go).
using StateView = evmone::state::StateView;
using BlockHashes = evmone::state::BlockHashes;
}  // namespace bcos::evmref
