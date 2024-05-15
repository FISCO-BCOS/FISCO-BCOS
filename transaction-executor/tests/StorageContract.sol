// SPDX-License-Identifier: MIT
pragma solidity ^0.8.24;

import "./StorageSlot.sol";

contract StorageContract {
    using StorageSlot for *;

    StorageSlot.AddressSlotType private addressSlot = StorageSlot.asAddress(keccak256("address_slot"));
    StorageSlot.BooleanSlotType private booleanSlot = StorageSlot.asBoolean(keccak256("boolean_slot"));
    StorageSlot.Bytes32SlotType private bytes32Slot = StorageSlot.asBytes32(keccak256("bytes32_slot"));
    StorageSlot.Uint256SlotType private uint256Slot = StorageSlot.asUint256(keccak256("uint256_slot"));
    StorageSlot.Int256SlotType private int256Slot = StorageSlot.asInt256(keccak256("int256_slot"));

    function setAddress(address _value) public {
        addressSlot.tstore(_value);
    }

    function getAddress() public view returns (address) {
        return addressSlot.tload();
    }

    function setBoolean(bool _value) public {
        booleanSlot.tstore(_value);
    }

    function getBoolean() public view returns (bool) {
        return booleanSlot.tload();
    }

    function setBytes32(bytes32 _value) public {
        bytes32Slot.tstore(_value);
    }

    function getBytes32() public view returns (bytes32) {
        return bytes32Slot.tload();
    }

    function setUint256(uint256 _value) public {
        uint256Slot.tstore(_value);
    }

    function getUint256() public view returns (uint256) {
        return uint256Slot.tload();
    }

    function setInt256(int256 _value) public {
        int256Slot.tstore(_value);
    }

    function getInt256() public view returns (int256) {
        return int256Slot.tload();
    }
}