// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;

abstract contract ContractAuthPrecompiled {
    function getAdmin(address path) public view returns (address);
    function resetAdmin(address path, address admin) public returns (int256);
    function setMethodAuthType(address path, bytes4 func, uint8 authType) public returns (int256);
    function openMethodAuth(address path, bytes4 func, address account) public returns (int256);
    function closeMethodAuth(address path, bytes4 func, address account) public returns (int256);
    function checkMethodAuth(address path, bytes4 func, address account) public view returns (bool);
    function getMethodAuth(address path, bytes4 func) public view returns (uint8,string[] memory,string[] memory);
    function setContractStatus(address _address, bool isFreeze) public returns(int);
    function contractAvailable(address _address) public view returns (bool);

    function deployType() public view returns (uint256);
    function setDeployAuthType(uint8 _type) public returns (int256);
    function openDeployAuth(address account) public returns (int256);
    function closeDeployAuth(address account) public returns (int256);
    function hasDeployAuth(address account) public view returns (bool);
}
