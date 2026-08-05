// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#include <bcos-evm/ledger/MemoryLedger.h>

#include <stdexcept>

namespace bcos::evm::ledger
{
std::optional<MemoryLedger::Account> MemoryLedger::get_account(
    const evmc::address& addr) const noexcept
{
    const auto it = m_accounts.find(addr);
    if (it == m_accounts.end())
        return std::nullopt;

    const auto& account = it->second;
    // KEEP: 存在但空的账户仍返回值(design §3/§4.4),has_storage 动态口径对齐 TestState。
    return Account{
        account.nonce, account.balance, evmone::keccak256(account.code), !account.storage.empty()};
}

evmc::bytes MemoryLedger::get_account_code(const evmc::address& addr) const noexcept
{
    const auto it = m_accounts.find(addr);
    if (it == m_accounts.end())
        return {};

    return it->second.code;
}

evmc::bytes32 MemoryLedger::get_storage(
    const evmc::address& addr, const evmc::bytes32& key) const noexcept
{
    const auto ait = m_accounts.find(addr);
    if (ait == m_accounts.end())
        return evmc::bytes32{};

    const auto& storage = ait->second.storage;
    const auto it = storage.find(key);
    return (it != storage.end()) ? it->second : evmc::bytes32{};
}

void MemoryLedger::applyDiff(const evmone::state::StateDiff& diff, bool seeding)
{
    // `seeding` 仅为与 Storage2Ledger::applyDiff 的接口统一而收(seedFromTestState 对两类
    // Ledger 同一句 `ledger.applyDiff(diff, true)`);MemoryLedger 无 D-6 空账户守卫,该参数
    // 无行为,忽略。
    (void)seeding;

    for (const auto& m : diff.modified_accounts)
    {
        // 账户 ensure-exists(design §5):无条件确保 entry 存在,即便本次无字段实际变化
        // (EIP-161 touch-only 账户 / 完全空账户播种)——不得优化为"无字段可写则跳过"。
        auto& account = m_accounts[m.addr];
        account.nonce = m.nonce;
        account.balance = m.balance;
        if (m.code.has_value())  // 契约③:code 无值不覆写
            account.code = *m.code;
        for (const auto& [key, value] : m.modified_storage)
        {
            if (value)  // 契约②:槽值为 0 = 删槽,不写零值
                account.storage.insert_or_assign(key, value);
            else
                account.storage.erase(key);
        }
    }

    for (const auto& addr : diff.deleted_accounts)
    {
        // strict 单形态(design §5):tripwire 内置,不提供 raw 版——view 中不存在即使用错误。
        const auto it = m_accounts.find(addr);
        if (it == m_accounts.end())
            throw std::runtime_error(
                "MemoryLedger::applyDiff: deleted_accounts entry not found in ledger (ghost "
                "delete, strict tripwire)");
        m_accounts.erase(it);
    }
}
}  // namespace bcos::evm::ledger
