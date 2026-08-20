// StateRootCompute 黄金向量测试——状态根引擎（替代退役 evmone mpt_hash）的回归防护。
// 空状态根 = keccak256(RLP("")) =
// 0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421 （以太坊规范
// emptyRootHash）。单账户 leaf = rlp(nonce, trimmed balance, storageRoot, codeHash) 用
// MemoryState（AccountVisitor 契约的最小实现）驱动 stateRootOf。
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/test/testutils/MemoryState.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <test/utils/test_state.hpp>

using namespace bcos::evm;
using namespace bcos::evm::evmstate;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
// evmc::bytes32 无 operator<<，比较用 hex 字符串（BOOST_CHECK_EQUAL 需要可打印类型）。
std::string toHexStr(const evmc::bytes32& v)
{
    return evmc::hex(v);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(StateRootComputeTests)

// Empty state → the canonical Ethereum empty-root hash. Pins the secure-trie empty path
// (keccak256(RLP(""))) end to end through computeTrieRoot.
BOOST_AUTO_TEST_CASE(empty_state_root_is_empty_root_hash)
{
    MemoryState ledger;
    auto root = stateRootOf(ledger);
    BOOST_CHECK_EQUAL(toHexStr(root),
        toHexStr(evmone::hash256{
            0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_bytes32}));
}

// Single account with nonce/balance/codeHash and one live storage slot — pins the account
// leaf construction (rlp(nonce, trimmed balance, storageRoot, codeHash)) and the secure-trie
// keying (keccak256(addr)) against a fixed golden value.
BOOST_AUTO_TEST_CASE(single_account_root_matches_golden)
{
    MemoryState ledger;
    constexpr auto kAddr = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto kSlot =
        0x0000000000000000000000000000000000000000000000000000000000000001_bytes32;
    constexpr auto kValue =
        0x000000000000000000000000000000000000000000000000000000000000002a_bytes32;
    ledger.applyDiff(evmone::state::StateDiff{.modified_accounts = {{.addr = kAddr,
                                                  .nonce = 1,
                                                  .balance = 1000_u256,
                                                  .code = std::nullopt,
                                                  .modified_storage = {{kSlot, kValue}}}},
        .deleted_accounts = {}});
    evmone::hash256 root;
    try
    {
        root = stateRootOf(ledger);
    }
    catch (const std::exception& e)
    {
        BOOST_FAIL(std::string("stateRootOf threw: ") + e.what());
    }
    // 黄金值（实现产出后固化；配合上面的空根规范值独立验证 + 与 evmone mpt_hash 的
    // 字段对拍，作为 leaf/trie 编码改动的回归防线——任何编码变化都会使此值变化）。
    BOOST_CHECK_EQUAL(toHexStr(root),
        toHexStr(evmone::hash256{
            0x26afd9d805a51c2d588429a29c18c3c23b14ba8c78b5f44be651867fc4815906_bytes32}));
}

BOOST_AUTO_TEST_SUITE_END()
