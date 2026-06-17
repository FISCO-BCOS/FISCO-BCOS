// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

/// @title IL2ValidatorSet
/// @notice On-chain validator set for FISCO-BCOS L2 mode, deployed as a predeploy
///         at 0x42000000000000000000000000000000000000C1.
/// @dev    The consensus address is the identity (mapping key + enumeration
///         element), so it is NOT a struct field. Single-record CRUD is O(1):
///         addition appends, removal swap-pops. The Validator struct packs
///         feeAddress(20) + jailed(1) + votingPower(8) into one slot.
interface IL2ValidatorSet {
    struct Validator {
        address payable feeAddress; // slot 0, byte 0-19
        bool jailed; // slot 0, byte 20  (Phase A: always false)
        uint64 votingPower; // slot 0, byte 21-28
        bytes consensusPublicKey; // slot 1 (dynamic) — algorithm-neutral
        uint256 incoming; // slot 2  (Phase A: always 0; Phase B fee accounting)
    }

    /// @notice Consensus addresses of the current validator set.
    function getValidators() external view returns (address[] memory);

    /// @notice Current validators paired with their consensus public keys.
    function getCurrentValidators() external view returns (address[] memory, bytes[] memory);

    /// @notice Whether `consensusAddress` is in the set.
    function isValidator(address consensusAddress) external view returns (bool);

    /// @notice Full record for a validator (zero-valued if absent).
    function getValidator(address consensusAddress) external view returns (Validator memory);

    /// @notice Owner-only: add a new validator. Reverts if it already exists.
    function addValidator(address consensusAddress, Validator calldata v) external;

    /// @notice Owner-only: remove a validator (O(1) swap-pop). Reverts if absent.
    function removeValidator(address consensusAddress) external;

    /// @notice Owner-only: overwrite an existing validator's record. Reverts if absent.
    function updateValidator(address consensusAddress, Validator calldata v) external;

    /// @notice Phase B placeholder: permanently jail a validator. Reverts in Phase A.
    function felony(address consensusAddress) external;

    /// @notice Phase B placeholder: temporarily penalize a validator. Reverts in Phase A.
    function misdemeanor(address consensusAddress) external;

    event ValidatorAdded(address indexed consensusAddress);
    event ValidatorRemoved(address indexed consensusAddress);
    event ValidatorUpdated(address indexed consensusAddress);
    event ValidatorJailed(address indexed consensusAddress);
}
