// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

/// @title ISystemConfig
/// @notice L2 chain configuration read by the FISCO-BCOS L2ConfigLoader (PR-4)
///         via a single staticcall to {getChainConfig}. Deployed as a predeploy
///         at 0x42000000000000000000000000000000000000C0 (the "0x42...00C0" slot).
interface ISystemConfig {
    /// @notice Aggregate getter: returns all four chain-config fields in one
    ///         call so L2ConfigLoader makes exactly one staticcall per block.
    /// @return chainId               the L2 chain id (immutable, fixed at deploy)
    /// @return l2BlockGasLimit       per-block gas limit
    /// @return compatibilityVersion  FISCO-BCOS compatibility version
    /// @return featureFlags          packed feature-flag bitfield
    function getChainConfig()
        external
        view
        returns (uint256 chainId, uint64 l2BlockGasLimit, uint32 compatibilityVersion, uint256 featureFlags);

    /// @notice Activation timestamp for a given hardfork id (0 = not scheduled).
    function getHardforkActivation(uint8 hardforkId) external view returns (uint64);

    /// @notice Owner-only: replace the packed feature-flag bitfield.
    function setFeatureFlag(uint256 newFlags) external;

    /// @notice Owner-only: schedule (or reschedule) a hardfork activation timestamp.
    function setHardforkActivation(uint8 id, uint64 timestamp) external;

    event FeatureFlagsUpdated(uint256 oldFlags, uint256 newFlags);
    event HardforkActivationUpdated(uint8 id, uint64 oldTs, uint64 newTs);
    event OwnershipTransferred(address indexed oldOwner, address indexed newOwner);
}
