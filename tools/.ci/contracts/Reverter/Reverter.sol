// Authored for the native CI suite (tools/.ci/scenarios, scenario 16): a contract whose
// methods revert with and without a reason string. Apache-2.0.
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;

contract Reverter {
    uint256 public counter;

    function ok() public returns (uint256) {
        counter += 1;
        return counter;
    }

    function fail() public {
        counter += 1;
        require(false, "reverted on purpose");
    }

    function failNoReason() public {
        counter += 1;
        revert();
    }
}
