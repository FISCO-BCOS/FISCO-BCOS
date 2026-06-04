// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import {IL2ValidatorSet} from "./interfaces/IL2ValidatorSet.sol";

/// @title L2ValidatorSet
/// @notice On-chain validator set predeploy for FISCO-BCOS L2 mode, in the BSC
///         ValidatorSet style.
/// @dev    Predeploy address: 0x42000000000000000000000000000000000000C1
///         ("0x42...00C1"). See storage-layout/L2ValidatorSet.json for the
///         golden storage fixture (PR-7 CI gate diffs it).
///
///         Storage layout (slots start at 0):
///           slot 0: owner (address)
///           slot 1: epoch (uint256)
///           slot 2: _currentValidators (address[] — length at slot 2, data hashed)
///           slot 3: _validatorByAddr (mapping)
///           slot 4: _isValidator (mapping)
///
///         Phase A maintains only the active set. {felony} / {misdemeanor} are
///         Phase B placeholders that revert; the Validator struct's `jailed`
///         (always false) and `incoming` (always 0) fields are reserved for the
///         Phase B slashing / fee-accounting governance.
contract L2ValidatorSet is IL2ValidatorSet {
    /// @notice Upper bound on the active set size. The contract targets small
    ///         consensus sets and does a full O(n) rotation on every update; this
    ///         cap keeps that rotation gas-bounded. A `constant` lives in code, not
    ///         storage, so it does not occupy a slot (storage-layout unchanged).
    uint256 public constant MAX_VALIDATORS = 256;

    address public owner;
    uint256 public epoch;
    address[] internal _currentValidators;
    mapping(address => Validator) internal _validatorByAddr;
    mapping(address => bool) internal _isValidator;

    modifier onlyOwner() {
        require(msg.sender == owner, "not owner");
        _;
    }

    /// @dev The initial `ValidatorSetUpdated` emitted via `_applyValidatorSet`
    ///      below happens only in tests / simulated deployments — a genesis
    ///      predeploy never runs this constructor on-chain, so that event is not
    ///      emitted at genesis.
    constructor(Validator[] memory initial, address _owner) {
        owner = _owner;
        _applyValidatorSet(initial);
    }

    /// @inheritdoc IL2ValidatorSet
    function getValidators() external view returns (address[] memory) {
        return _currentValidators;
    }

    /// @inheritdoc IL2ValidatorSet
    function getCurrentValidators() external view returns (address[] memory, bytes[] memory) {
        address[] memory cur = _currentValidators;
        bytes[] memory bls = new bytes[](cur.length);
        for (uint256 i = 0; i < cur.length; i++) {
            bls[i] = _validatorByAddr[cur[i]].BLSPublicKey;
        }
        return (cur, bls);
    }

    /// @inheritdoc IL2ValidatorSet
    function isCurrentValidator(address a) external view returns (bool) {
        return _isValidator[a];
    }

    /// @inheritdoc IL2ValidatorSet
    function getValidator(address a) external view returns (Validator memory) {
        return _validatorByAddr[a];
    }

    /// @inheritdoc IL2ValidatorSet
    /// @dev The contract targets small consensus sets: rotation is full O(n) over
    ///      the set, so the new set must be at most {MAX_VALIDATORS}.
    function updateValidatorSet(Validator[] calldata next) external onlyOwner {
        _applyValidatorSet(next);
    }

    /// @inheritdoc IL2ValidatorSet
    function felony(address) external {
        revert("not implemented in Phase A");
    }

    /// @inheritdoc IL2ValidatorSet
    function misdemeanor(address) external {
        revert("not implemented in Phase A");
    }

    /// @notice Owner-only: hand ownership to a new non-zero address.
    function transferOwnership(address newOwner) external onlyOwner {
        require(newOwner != address(0), "zero owner");
        owner = newOwner;
    }

    /// @dev Replace the active set: clear the old set, install `next` (rejecting
    ///      duplicate consensus addresses), bump the epoch, and emit the update.
    ///      Targets small consensus sets — full O(n) rotation, capped at
    ///      {MAX_VALIDATORS} to keep the gas cost bounded.
    function _applyValidatorSet(Validator[] memory next) internal {
        require(next.length <= MAX_VALIDATORS, "validator set too large");
        for (uint256 i = 0; i < _currentValidators.length; i++) {
            _isValidator[_currentValidators[i]] = false;
            delete _validatorByAddr[_currentValidators[i]];
        }
        delete _currentValidators;
        for (uint256 i = 0; i < next.length; i++) {
            address a = next[i].consensusAddress;
            require(!_isValidator[a], "duplicate validator");
            _isValidator[a] = true;
            _validatorByAddr[a] = next[i];
            _currentValidators.push(a);
        }
        unchecked {
            epoch += 1;
        }
        emit ValidatorSetUpdated(epoch, _currentValidators);
    }
}
