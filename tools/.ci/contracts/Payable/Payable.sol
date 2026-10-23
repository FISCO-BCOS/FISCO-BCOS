// Authored for the native CI suite (tools/.ci/scenarios, scenario 11): the same ABI as
// java-sdk's integration-test PayableTest (src/integration-test/java/org/fisco/bcos/sdk/v3/
// test/contract/solidity/PayableTest.java, commit e5ec92365d59298e9dd95ca7563f335c3556f9ac),
// whose source is not in that repository. Apache-2.0.
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;

contract Payable {
    uint256 public value;

    event incEvent(string msg);

    constructor() public payable {
        value = 0;
    }

    function inc(string memory mesg) public returns (uint256) {
        value += 1;
        emit incEvent(mesg);
        return value;
    }

    function incWithPayable(string memory mesg) public payable returns (uint256) {
        value += 1;
        emit incEvent(mesg);
        return value;
    }
}
