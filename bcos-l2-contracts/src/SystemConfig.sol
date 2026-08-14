// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import {ISystemConfig} from "./interfaces/ISystemConfig.sol";
import {OwnableUpgradeable} from "@openzeppelin/contracts-upgradeable/access/OwnableUpgradeable.sol";

/// @title SystemConfig
/// @notice Generic key→value system-config predeploy for FISCO-BCOS L2 mode at
///         0x43000000000000000000000000000000000000C0.
/// @dev    Storage is one standard `mapping(string => Entry)`; each Entry packs
///         (value:uint192, enableNumber:uint64) into a single slot. The L2 upper
///         layers read a key's slot directly (no EVM) via
///         `keccak256(utf8(key) ‖ be32(baseSlot))`, where baseSlot is the slot of
///         `_config` (pinned by the storage-layout fixture). getValueByKey is the
///         EVM-callable path for external callers.
///
///         AUTHORITY (two independent slots, two independent roles):
///         the ERC-1967 admin slot holds ProxyAdmin (upgrade authority only),
///         while `Ownable.owner` (slot 51) holds the L2 governance entity that
///         may call setValueByKey. Genesis tooling writes both slots and must
///         never point them at the same entity.
///
///         Runtime writes are restricted to a key whitelist (D4 authority
///         boundary): `chain_id` is chain identity, immutable after genesis;
///         `gas_limit` authority is L1 SystemConfig → op-node payload
///         attributes → execution layer, never this contract;
///         `compatibility_version` / `feature_flags` change state-transition
///         semantics, so a runtime write would fork FISCO from the OP oracle.
///         `chain_id` and `gas_limit` are frozen permanently;
///         `compatibility_version` and `feature_flags` are frozen until a
///         governed change path that keeps the OP oracle in sync exists
///         (tracked in the integration plan). Widening the whitelist requires
///         a contract upgrade, i.e. explicit governance review.
///
///         GENESIS: as a predeploy the constructor/initialize do not run on-chain;
///         genesis tooling writes the owner slot (OZ) and each key's packed Entry
///         slot directly into allocs.
contract SystemConfig is ISystemConfig, OwnableUpgradeable {
    struct Entry {
        uint192 value;
        uint64 enableNumber;
    }

    /// @dev baseSlot is fixed by declaration order after the OZ upgradeable
    ///      base storage; pinned in storage-layout/SystemConfig.json.
    mapping(string => Entry) private _config;

    /// @notice Set the deploy-time owner (the L2 governance entity — NOT the
    ///         ProxyAdmin, which only holds the ERC-1967 admin slot). Not run
    ///         at genesis — see contract @dev note.
    function initialize(address owner_) external initializer {
        __Ownable_init();
        _transferOwnership(owner_);
    }

    /// @inheritdoc ISystemConfig
    function setValueByKey(string calldata key, uint192 value, uint64 enableNumber)
        external
        onlyOwner
    {
        require(_isWritableKey(key), "SystemConfig: key not runtime-writable");
        _config[key] = Entry(value, enableNumber);
        emit ConfigUpdate(key, value, enableNumber);
    }

    /// @inheritdoc ISystemConfig
    function getValueByKey(string calldata key)
        external
        view
        returns (uint192 value, uint64 enableNumber)
    {
        Entry storage e = _config[key];
        return (e.value, e.enableNumber);
    }

    /// @dev keccak256 of the only runtime-writable key. Computed at compile
    ///      time; constants occupy no storage slot, so the storage-layout
    ///      fixture is unaffected.
    bytes32 private constant WRITABLE_KEY_HASH = keccak256("block_tx_count_limit");

    /// @dev Whitelist of runtime-writable keys — see the contract @dev note
    ///      for why chain_id / gas_limit / compatibility_version /
    ///      feature_flags (and every unknown key) are genesis-frozen.
    function _isWritableKey(string calldata key) internal pure returns (bool) {
        return keccak256(bytes(key)) == WRITABLE_KEY_HASH;
    }
}
