// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import {ISystemConfig} from "./interfaces/ISystemConfig.sol";
import {OwnableUpgradeable} from "@openzeppelin/contracts-upgradeable/access/OwnableUpgradeable.sol";

/// @title SystemConfig
/// @notice Generic key→value system-config predeploy for FISCO-BCOS L2 mode at
///         0x42000000000000000000000000000000000000C0.
/// @dev    Storage is one standard `mapping(string => Entry)`; each Entry packs
///         (value:uint192, enableNumber:uint64) into a single slot. The L2 upper
///         layers read a key's slot directly (no EVM) via
///         `keccak256(utf8(key) ‖ be32(baseSlot))`, where baseSlot is the slot of
///         `_config` (pinned by the storage-layout fixture). getValueByKey is the
///         EVM-callable path for external callers; writes are owner-only and the
///         owner is the ProxyAdmin governance behind the ERC-1967 proxy.
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

    /// @notice Set the deploy-time owner (ProxyAdmin in production). Not run at
    ///         genesis — see contract @dev note.
    function initialize(address owner_) external initializer {
        __Ownable_init();
        _transferOwnership(owner_);
    }

    /// @inheritdoc ISystemConfig
    function setValueByKey(string calldata key, uint192 value, uint64 enableNumber)
        external
        onlyOwner
    {
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
}
