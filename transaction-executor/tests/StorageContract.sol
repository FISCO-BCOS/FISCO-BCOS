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

    function storeIntTest(int256 _value) public returns (int256) {
        int256Slot.tstore(_value);
        return int256Slot.tload();
    }
}
// Path: transaction-executor/tests/ContractA.sol
pragma solidity ^0.8.25;

import "./StorageSlot.sol";
import "./ContractB.sol";

contract ContractA {
    using StorageSlot for *;

    StorageSlot.Int256SlotType private intSlot;
    constructor(int256 value){
        StorageSlot.tstore(intSlot, value);
    }

    function getData() public view returns (int256) {
        return StorageSlot.tload(intSlot);
    }

    function callContractB() public returns (int256){
        ContractB b = new ContractB();
        return b.callContractA(address(this));
    }

}

// Path: transaction-executor/tests/ContractA.sol
import "./StorageSlot.sol";
import "./ContractA.sol";


contract ContractB {

    function callContractA(address a) public returns (int256){
        ContractA a = ContractA(a);
        int256 result = a.getData();
        return result;
    }

}
// Path: transaction-executor/tests/MainContract.sol
import "./StorageSlot.sol";
import "./ContractA.sol";
import "./ContractB.sol";

contract MainContract {


    function checkAndVerifyIntValue(int256 value) public returns (bool) {
        ContractA a = new ContractA(value);
        int256 result = a.callContractB();
        require(result == value, "store value not equal tload result");
        return true;
    }
}