// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import {IL2ValidatorSet} from "./interfaces/IL2ValidatorSet.sol";
import {OwnableUpgradeable} from "@openzeppelin/contracts-upgradeable/access/OwnableUpgradeable.sol";

/// @title L2ValidatorSet
/// @notice On-chain validator set predeploy for FISCO-BCOS L2 mode, BSC
///         ValidatorSet style, at 0x42000000000000000000000000000000000000C1.
/// @dev    Access control / upgradeability use OZ `OwnableUpgradeable` behind an
///         ERC-1967 proxy administered by ProxyAdmin — consistent with SystemConfig
///         and the vendored OP predeploys. The validator storage (active set +
///         lookup maps) is unchanged from the Phase A design; only the owner
///         mechanism moved from hand-rolled to OZ.
///
///         Storage: the OZ v4.7 upgradeable base occupies slots 0-100
///         (Initializable[0] + ContextUpgradeable __gap[1-50] + _owner[51] +
///         __gap[52-100]); this contract's validator storage starts at slot 101.
///
///         Phase A maintains only the active set. {felony}/{misdemeanor} are Phase
///         B placeholders that revert; the Validator struct's `jailed` (always
///         false) / `incoming` (always 0) fields are reserved for Phase B.
///
///         GENESIS: as a predeploy the constructor/initialize do not run on-chain;
///         genesis tooling writes the owner slot and validator storage into allocs.
contract L2ValidatorSet is IL2ValidatorSet, OwnableUpgradeable {
    /// @notice Upper bound on the active set size; rotation is O(n), so this caps
    ///         the per-update gas. A `constant` lives in code, not storage.
    uint256 public constant MAX_VALIDATORS = 256;

    uint256 public epoch;
    address[] internal _currentValidators;
    mapping(address => Validator) internal _validatorByAddr;
    mapping(address => bool) internal _isValidator;

    /// @notice Set the owner (ProxyAdmin in production) and install the initial
    ///         validator set. Not run at genesis — see contract @dev note.
    function initialize(Validator[] memory initial, address owner_) external initializer {
        __Ownable_init();
        _transferOwnership(owner_);
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

    /// @dev Replace the active set: clear the old set, install `next` (rejecting
    ///      duplicate consensus addresses), bump the epoch, emit the update.
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
