/**
 * @file: SimpleStartDemo.sol
 * @author: fisco-dev
 * 
 * @date: 2017
 */

pragma solidity ^0.4.2;

contract SimpleStartDemo {
    int256 storedData;
    event AddMsg(address indexed sender, bytes32 msg);
    function SimpleStartDemo() {
        storedData = 5;
    }

    function setData(int256 x) public{
        storedData = x;
        AddMsg(msg.sender, "[in the set() method]");
    }

    function getData() constant public returns (int256 _ret) {
        AddMsg(msg.sender, "[in the get() method]");
        return _ret = storedData;
    }
}
