//SPDX-License-Identifier: Apache2.0
pragma solidity^0.6.10;

interface  CounterPrecompiled {
    function init(string memory _keyAddr) external;
    function increment(string memory _keyAddr) external;
    function incrementBy(string memory _keyAddr, uint256 _value) external;
    function reset(string memory _keyAddr) external;
    function get(string memory _keyAddr) external view returns(uint256);
}
