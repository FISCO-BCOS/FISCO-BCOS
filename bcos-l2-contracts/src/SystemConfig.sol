// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import {ISystemConfig} from "./interfaces/ISystemConfig.sol";

/// @title SystemConfig
/// @notice L2 chain configuration predeploy for FISCO-BCOS L2 mode.
/// @dev    Predeploy address: 0x42000000000000000000000000000000000000C0
///         ("0x42...00C0"). PR-4's L2ConfigLoader reads it once per block via a
///         single staticcall to {getChainConfig}
///         (selector `0x606c0c94` == `cast sig "getChainConfig()"`).
///
///         Storage layout (see storage-layout/SystemConfig.json golden fixture;
///         CI gate in PR-7 diffs that file). NOTE: `chainId` is `immutable`, so
///         it lives in the contract code, NOT in storage — it does not occupy a
///         storage slot and does not appear in the storage-layout fixture. The
///         storage variables below occupy slots starting at slot 0:
///           slot 0: l2BlockGasLimit (uint64) + compatibilityVersion (uint32)  [packed]
///           slot 1: featureFlags (uint256)
///           slot 2: owner (address)
///           slot 3: _hardforkActivations (mapping)
contract SystemConfig is ISystemConfig {
    /// @dev immutable: fixed at deploy, lives in code (no storage slot, no setter).
    /// @dev GENESIS CONSTRAINT: `chainId` is immutable — it lives in the runtime
    /// bytecode (solc `immutableReferences`), NOT in storage. A genesis predeploy
    /// never executes this constructor, so the genesis tooling (build-allocs.py)
    /// MUST patch the artifact's deployedBytecode at the offsets listed in
    /// out/SystemConfig.sol/SystemConfig.json -> deployedBytecode.immutableReferences,
    /// otherwise on-chain `getChainConfig()` returns chainId == 0.
    uint256 public immutable chainId;

    uint64 public l2BlockGasLimit;
    uint32 public compatibilityVersion;
    uint256 public featureFlags;
    address public owner;
    mapping(uint8 => uint64) internal _hardforkActivations;

    modifier onlyOwner() {
        require(msg.sender == owner, "not owner");
        _;
    }

    /// @dev The `OwnershipTransferred(address(0), _owner)` emission below happens
    ///      only in tests / simulated deployments — a genesis predeploy never runs
    ///      this constructor on-chain, so the event is not emitted at genesis.
    constructor(uint256 _chainId, uint64 _gasLimit, uint32 _version, uint256 _featureFlags, address _owner) {
        chainId = _chainId;
        l2BlockGasLimit = _gasLimit;
        compatibilityVersion = _version;
        featureFlags = _featureFlags;
        owner = _owner;
        emit OwnershipTransferred(address(0), _owner);
    }

    /// @inheritdoc ISystemConfig
    function getChainConfig() external view returns (uint256, uint64, uint32, uint256) {
        return (chainId, l2BlockGasLimit, compatibilityVersion, featureFlags);
    }

    /// @inheritdoc ISystemConfig
    function getHardforkActivation(uint8 id) external view returns (uint64) {
        return _hardforkActivations[id];
    }

    /// @inheritdoc ISystemConfig
    function setFeatureFlag(uint256 newFlags) external onlyOwner {
        emit FeatureFlagsUpdated(featureFlags, newFlags);
        featureFlags = newFlags;
    }

    /// @inheritdoc ISystemConfig
    function setHardforkActivation(uint8 id, uint64 timestamp) external onlyOwner {
        emit HardforkActivationUpdated(id, _hardforkActivations[id], timestamp);
        _hardforkActivations[id] = timestamp;
    }

    /// @notice Owner-only: hand ownership to a new non-zero address.
    function transferOwnership(address newOwner) external onlyOwner {
        require(newOwner != address(0), "zero owner");
        emit OwnershipTransferred(owner, newOwner);
        owner = newOwner;
    }
}
