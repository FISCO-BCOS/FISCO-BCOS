// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/console
// Path: src/main/resources/contract/solidity/HelloWorld.sol
// Commit: fc2a37b0e34b27cfb8cfad6cd4f4d18bf576dfdf
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
pragma solidity >=0.6.10 <=0.8.26;

contract HelloWorld {
    string name;

    constructor() public {
        name = "Hello, World!";
    }

    function get() public view returns (string memory) {
        return name;
    }

    function set(string memory n) public {
        name = n;
    }
}