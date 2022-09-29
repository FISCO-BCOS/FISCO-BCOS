// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;

abstract contract AccountManager {
    // 创建账户，并默认将状态设置为0, 只有治理委员可以调用
    function createAccount(string memory addr) public virtual returns (int32);
    // 设置账户状态，只有治理委员可以调用，0 - normal, others - abnormal
    function setAccountStatus(string memory addr, uint16 status) public virtual returns (int32);
    // 任何用户都可以调用
    function getAccountStatus(string memory addr) public view virtual returns (uint16,uint256);
}

abstract contract Account {
    // 设置账户状态，只有AccountManager可以调用， 0 - normal, others - abnormal
    function setAccountStatus(uint16 status) public view virtual returns (int32);
    // 任何用户都可以调用
    function getAccountStatus() public view virtual returns (uint16,uint256);
}
