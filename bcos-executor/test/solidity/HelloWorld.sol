// SPDX-License-Identifier: Apache 2.0
pragma solidity ^0.8.1;

contract HelloWorld {
    string m_value = "Hello World!";

    function get() public view returns (string memory) {
        return m_value;
    }

    function set(string memory value) public {
        m_value = value;
    }
}