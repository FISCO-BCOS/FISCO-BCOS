// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/StaticCall.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
pragma solidity >=0.6.0 <0.8.12;

import "./ProxyTest.sol";

contract StaticCall {
    HelloWorld helloWorld = new HelloWorld();
    event Result(bytes);
    event Result(uint);

    modifier initIfNot() {
        if (address(helloWorld) == address(0)) {
            helloWorld = new HelloWorld();
        }
        _;
    }

    function staticCallASM(address target, bytes memory data)
    internal
    view
    returns (bool result)
    {
        assembly {
            result := staticcall(gas(), target, add(data, 0x20), mload(data), mload(0x40), 0)
        }
        return result;
    }

    function staticCallUintASM(address target, bytes memory data)
    internal
    view
    returns (bool, uint)
    {
        uint ret;
        bool result;
        assembly {
            let size := 0x20
            let free := mload(0x40)
            result := staticcall(gas(), target, add(data, 0x20), mload(data), free, size)
            ret := mload(free)
        }
        return (result, ret);
    }

    function getAddress() public view returns (address) {
        return address(helloWorld);
    }

    function testOk() public returns(bytes memory){
        (bool ok, bytes memory result) = address(helloWorld).staticcall(abi.encodeWithSignature("get()"));
        require(ok);
        emit Result(result);
        return result;
    }

    function testFailed() public {
        (bool ok, bytes memory result) = address(helloWorld).staticcall(abi.encodeWithSignature("set(string)", "aaa"));
        require(ok, "staticcall must call view/pure function");
    }

    function testASMOk() public returns(uint){
        bytes memory data = abi.encodeWithSignature("getNum()");
        bool ok = staticCallASM(address(helloWorld), data);
        require(ok);
        uint ret;
        (ok, ret) = staticCallUintASM(address(helloWorld), data);
        require(ok);
        require(ret == 100);
        emit Result(ret);
        return ret;
    }

    function testASMFailed() public {
        bytes memory data = abi.encodeWithSignature("setNum(uint256)", 222);
        bool ok = staticCallASM(address(helloWorld), data);
        require(!ok);
    }

    function testASMUintFailed() public {
        bytes memory data = abi.encodeWithSignature("setNum(uint256)", 222);
        (bool ok, uint ret) = staticCallUintASM(address(helloWorld), data);
        require(!ok);
    }

    function testEmptyAddr() public {
        (bool ok, bytes memory result) = address(0x10016666666).staticcall(abi.encodeWithSignature("get()"));
        require(ok, "addr not exist but must return ok to be the same as eth");
        require(result.length == 0, "result must be empty");
    }

    function checkOk() public initIfNot {
        (bool ok, bytes memory result) = address(helloWorld).staticcall(abi.encodeWithSignature("get()"));
        require(ok);

        testASMOk();
    }

    function checkFailed() public {
        testEmptyAddr();

        (bool ok, bytes memory result) = address(helloWorld).staticcall(abi.encodeWithSignature("set(string)", "aaa"));
        require(!ok, "staticcall a state write function must return not ok");
        require(result.length == 0, string(abi.encodePacked("staticcall failed for has result: ", result)));

        testASMFailed();
        testASMUintFailed();
    }

    event Info(string, uint256);
    function check() public {
        uint256 gasBefore = gasleft();
        checkOk();
        uint256 gasAfter = gasleft();
        emit Info("gas used on checkOk(): ", gasBefore - gasAfter);

        Proxy proxy = new ProxyImpl(address(this));
        gasBefore = gasleft();
        (bool success, bytes memory reason) = address(proxy).call(abi.encodeWithSignature("checkOk()"));
        gasAfter = gasleft();
        require(success, string(abi.encodePacked("checkOk() by proxy failed: ", reason)));
        emit Info("gas used on checkOk() by proxy: ", gasBefore - gasAfter);

        // staticcall failed will cost half of the gas
        // There are many staticcall failed in checkFailed(), will cost most of the gas. So we need to put it in the end.
        gasBefore = gasleft();
        (success, reason) = address(proxy).call(abi.encodeWithSignature("checkFailed()"));
        gasAfter = gasleft();
        require(success, string(abi.encodePacked("checkFailed() by proxy failed: ", reason)));
        emit Info("gas used on checkFailed() by proxy: ", gasBefore - gasAfter);
    }

    function get() public view returns (string memory) {
        return helloWorld.get();
    }

    function set(string memory n) public {
        helloWorld.set(n);
    }
}

contract HelloWorld {
    string name;
    uint num = 100;

    constructor() public {
        name = "Hello, World!";
    }

    function get() public view returns (string memory) {
        return name;
    }

    function set(string memory n) public {
        name = n;
    }

    function getNum() public view returns (uint) {
        return num;
    }

    function setNum(uint n) public {
        num = n;
    }
}