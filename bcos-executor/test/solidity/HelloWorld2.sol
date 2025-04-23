// SPDX-License-Identifier: Apache 2.0
pragma solidity ^0.8.1;

import "./HelloWorld.sol";

contract HelloWorld2 {
    HelloWorld h1;

    constructor() {
        h1 = new HelloWorld();
    }

    function getHello() public view returns (address) {
        return address(h1);
    }

    function get() public view returns (string memory) {
        return h1.get();
    }

    function set(string memory value) public {
        h1.set(value);
    }
}