/**
 * @file: SimpleStorage.sol
 * @author: fisco-dev
 * 
 * @date: 2017
 */

pragma solidity ^0.4.2;

contract SimpleStorage {
    uint storedData;

    function SimpleStorage() {
        storedData = 5;
    }

    function set(uint x) public {
        storedData = x;
    }

    function get() constant public returns (uint _ret) {
        _ret = storedData;
    }
}
