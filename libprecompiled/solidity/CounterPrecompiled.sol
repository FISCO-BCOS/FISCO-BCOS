//SPDX-License-Identifier: Apache2.0
pragma solidity^0.6.10;

interface  CounterPrecompiled {
    function init(string memory _key) external;
    function increment(string memory _key) external;
    function incrementBy(string memory _key, uint256 _value) external;
    function reset(string memory _key) external;
    function get(string memory _key) external view returns(uint256);
}
