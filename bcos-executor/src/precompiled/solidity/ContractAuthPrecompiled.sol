// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;

enum Status{
    normal,
    freeze,
    abolish
}

abstract contract ContractAuthPrecompiled {
    function getAdmin(address contractAddr) public view virtual returns (address);
    function resetAdmin(address contractAddr, address admin) public virtual returns (int256);
    function setMethodAuthType(address contractAddr, bytes4 func, uint8 authType) public virtual returns (int256);
    function openMethodAuth(address contractAddr, bytes4 func, address account) public virtual returns (int256);
    function closeMethodAuth(address contractAddr, bytes4 func, address account) public virtual returns (int256);
    function checkMethodAuth(address contractAddr, bytes4 func, address account) public view virtual returns (bool);
    function getMethodAuth(address contractAddr, bytes4 func) public view virtual returns (uint8,string[] memory,string[] memory);
    function setContractStatus(address _address, bool isFreeze) public virtual returns(int);
    function setContractStatus(address _address, Status _status) public virtual returns(int);
    function contractAvailable(address _address) public view virtual returns (bool);

    function deployType() public view virtual returns (uint256);
    function setDeployAuthType(uint8 _type) public virtual returns (int256);
    function openDeployAuth(address account) public virtual returns (int256);
    function closeDeployAuth(address account) public virtual returns (int256);
    function hasDeployAuth(address account) public view virtual returns (bool);
    function initAuth(string memory account) public virtual returns(int256);
}
