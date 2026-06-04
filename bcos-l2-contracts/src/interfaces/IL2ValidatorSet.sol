// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

/// @title IL2ValidatorSet
/// @notice On-chain validator set for FISCO-BCOS L2 mode, in the BSC
///         ValidatorSet style. Deployed as a predeploy at
///         0x42000000000000000000000000000000000000C1 (the "0x42...00C1" slot).
/// @dev    Phase A only exposes/maintains the active set. The `jailed` and
///         `incoming` fields are kept in the struct layout for Phase B
///         (slashing / fee accounting) but are constant in Phase A:
///         `jailed` is always false and `incoming` is always 0.
interface IL2ValidatorSet {
    struct Validator {
        address consensusAddress;
        address payable feeAddress;
        bytes BLSPublicKey;
        uint64 votingPower;
        bool jailed; // Phase A: always false
        uint256 incoming; // Phase A: always 0
    }

    /// @notice Consensus addresses of the current active validator set.
    function getValidators() external view returns (address[] memory);

    /// @notice Current validators paired with their BLS public keys.
    function getCurrentValidators() external view returns (address[] memory, bytes[] memory);

    /// @notice Whether `validator` is in the current active set.
    function isCurrentValidator(address validator) external view returns (bool);

    /// @notice Full record for a validator (zero-valued if not in the set).
    function getValidator(address validator) external view returns (Validator memory);

    /// @notice Owner-only: replace the active validator set, bumping the epoch.
    function updateValidatorSet(Validator[] calldata next) external;

    /// @notice Phase B placeholder: permanently jail a validator. Reverts in Phase A.
    function felony(address validator) external;

    /// @notice Phase B placeholder: temporarily penalize a validator. Reverts in Phase A.
    function misdemeanor(address validator) external;

    event ValidatorSetUpdated(uint256 epoch, address[] validators);
    event ValidatorJailed(address indexed validator);
}
