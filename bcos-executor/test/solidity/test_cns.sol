pragma solidity ^0.4.25;

import "./precompiled/CNSPrecompiled.sol";

contract HelloWorld {
    string public name;
    constructor() public {name = "Hello, World!";}
    function set(string n) public {name = n;}

    function get() public returns (string) {return name;}

    function getThis() constant public returns (address) {return this;}
}

contract TestCNS {
    CNSPrecompiled cns;
    HelloWorld hello;
    string contractName;
    address contractAddress;
    string abiStr;
    string version;
    constructor(){
        cns = CNSPrecompiled(0x1004);
        hello = new HelloWorld();
        contractName = "HelloWorld";
        contractAddress = hello.getThis();
        abiStr = "[{\"constant\":false,\"inputs\":[{\"name\":\"num\",\"type\":\"uint256\"}],\"name\":\"trans\",\"outputs\":[],\"payable\":false,\"type\":\"function\"},{\"constant\":true,\"inputs\":[],\"name\":\"get\",\"outputs\":[{\"name\":\"\",\"type\":\"uint256\"}],\"payable\":false,\"type\":\"function\"},{\"inputs\":[],\"payable\":false,\"type\":\"constructor\"}]";
        version = "1.0";
    }
    function helloTest() returns (bool){
        uint256 result1 = cns.insert(contractName, version, contractAddress, abiStr);
        if (result1 != 0) return false;
        address helloAddress;
        string memory abiGot;
        (helloAddress, abiGot) = cns.selectByNameAndVersion(contractName, version);
        if (helloAddress != contractAddress) return false;
        return true;
    }

    function insertTest(string name, string v, address addr, string a) returns (uint256){
        uint256 result = cns.insert(name, v, addr, a);
        return result;
    }

    function selectTest(string name, string v) returns(address,string)  {
        return cns.selectByNameAndVersion(name, version);
    }
}



