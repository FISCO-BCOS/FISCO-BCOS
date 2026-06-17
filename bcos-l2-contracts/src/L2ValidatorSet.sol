// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import {IL2ValidatorSet} from "./interfaces/IL2ValidatorSet.sol";
import {OwnableUpgradeable} from "@openzeppelin/contracts-upgradeable/access/OwnableUpgradeable.sol";
import {EnumerableSet} from "@openzeppelin/contracts/utils/structs/EnumerableSet.sol";

/// @title L2ValidatorSet
/// @notice On-chain validator set predeploy for FISCO-BCOS L2 mode at
///         0x42000000000000000000000000000000000000C1.
/// @dev    Access control / upgradeability use OZ `OwnableUpgradeable` behind an
///         ERC-1967 proxy administered by ProxyAdmin, consistent with SystemConfig
///         and the vendored OP predeploys.
///
///         Membership + enumeration use OZ `EnumerableSet.AddressSet` (O(1)
///         add/remove/contains, swap-pop removal); the per-validator record hangs
///         off a parallel `mapping(address => Validator)`. The consensus address is
///         the identity (set element + mapping key), so it is not a struct field.
///
///         Storage: the OZ v4.7 upgradeable base occupies slots 0-100; this
///         contract's storage starts at slot 101.
///
///         Phase A: {felony}/{misdemeanor} revert; Validator.jailed is always
///         false and Validator.incoming always 0 (reserved for Phase B).
///
///         GENESIS: as a predeploy the constructor/initialize do not run on-chain;
///         genesis tooling writes the owner slot and validator storage into allocs.
contract L2ValidatorSet is IL2ValidatorSet, OwnableUpgradeable {
    using EnumerableSet for EnumerableSet.AddressSet;

    EnumerableSet.AddressSet private _validatorSet;
    mapping(address => Validator) private _validatorByAddr;

    /// @notice Set the owner (ProxyAdmin in production) and install the initial
    ///         validator set. Not run at genesis — see contract @dev note.
    function initialize(address[] memory addrs, Validator[] memory vals, address owner_)
        external
        initializer
    {
        require(addrs.length == vals.length, "length mismatch");
        __Ownable_init();
        _transferOwnership(owner_);
        for (uint256 i = 0; i < addrs.length; i++) {
            _add(addrs[i], vals[i]);
        }
    }

    /// @inheritdoc IL2ValidatorSet
    function getValidators() external view returns (address[] memory) {
        return _validatorSet.values();
    }

    /// @inheritdoc IL2ValidatorSet
    function getCurrentValidators() external view returns (address[] memory, bytes[] memory) {
        address[] memory cur = _validatorSet.values();
        bytes[] memory keys = new bytes[](cur.length);
        for (uint256 i = 0; i < cur.length; i++) {
            keys[i] = _validatorByAddr[cur[i]].consensusPublicKey;
        }
        return (cur, keys);
    }

    /// @inheritdoc IL2ValidatorSet
    function isValidator(address a) public view returns (bool) {
        return _validatorSet.contains(a);
    }

    /// @inheritdoc IL2ValidatorSet
    function getValidator(address a) external view returns (Validator memory) {
        return _validatorByAddr[a];
    }

    /// @inheritdoc IL2ValidatorSet
    function addValidator(address a, Validator calldata v) external onlyOwner {
        _add(a, v);
    }

    /// @inheritdoc IL2ValidatorSet
    function removeValidator(address a) external onlyOwner {
        require(_validatorSet.remove(a), "not a validator");
        delete _validatorByAddr[a];
        emit ValidatorRemoved(a);
    }

    /// @inheritdoc IL2ValidatorSet
    function updateValidator(address a, Validator calldata v) external onlyOwner {
        require(_validatorSet.contains(a), "not a validator");
        _validatorByAddr[a] = v;
        emit ValidatorUpdated(a);
    }

    /// @inheritdoc IL2ValidatorSet
    function felony(address) external {
        revert("not implemented in Phase A");
    }

    /// @inheritdoc IL2ValidatorSet
    function misdemeanor(address) external {
        revert("not implemented in Phase A");
    }

    function _add(address a, Validator memory v) internal {
        require(a != address(0), "zero address");
        require(_validatorSet.add(a), "duplicate validator");
        _validatorByAddr[a] = v;
        emit ValidatorAdded(a);
    }
}
