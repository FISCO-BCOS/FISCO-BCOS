// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

/// @title ISystemConfig
/// @notice Generic key→value system-config store for FISCO-BCOS L2 mode,
///         deployed as a predeploy at 0x42000000000000000000000000000000000000C0.
/// @dev    Each config key maps to a packed Entry{value, enableNumber}. The L2
///         upper layers (executor/consensus) do NOT call getValueByKey via EVM;
///         they read the backing storage slot directly using the fixed addressing
///         formula `keccak256(utf8(key) ‖ be32(baseSlot))` (zero EVM per block).
///         getValueByKey/setValueByKey are the EVM-callable path for external
///         contracts / RPC / governance. See the slot-KV redesign spec.
interface ISystemConfig {
    /// @notice Emitted on every config write (owner-only, via ProxyAdmin governance).
    event ConfigUpdate(string indexed key, uint192 value, uint64 enableNumber);

    /// @notice Owner-only: set a config value and the block height it takes effect at.
    /// @param key         config name (e.g. "web3_chain_id", "feature_evm_prague")
    /// @param value       config value, ≤ uint192 (all current configs fit)
    /// @param enableNumber block height at which the value becomes active
    function setValueByKey(string calldata key, uint192 value, uint64 enableNumber) external;

    /// @notice Read a config value and its activation block height.
    /// @return value        the stored value (0 if key unset)
    /// @return enableNumber the activation block height (0 if key unset)
    function getValueByKey(string calldata key)
        external
        view
        returns (uint192 value, uint64 enableNumber);
}
