// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;

enum AccountStatus{
    normal,
    freeze,
    abolish
}

abstract contract AccountManager {
    // 设置账户状态，只有治理委员可以调用，0 - normal, others - abnormal, 如果账户不存在会先创建
    function setAccountStatus(address addr, AccountStatus status) public virtual returns (int32);
    // 任何用户都可以调用
    function getAccountStatus(address addr) public view virtual returns (AccountStatus);
}

abstract contract Account {
    // 设置账户状态，只有AccountManager可以调用， 0 - normal, others - abnormal
    function setAccountStatus(AccountStatus status) public virtual returns (int32);
    // 任何用户都可以调用
    function getAccountStatus() public view virtual returns (AccountStatus);

    // 设置账户余额，内部接口，用户不可调用
    function setAccountBalance(uint256) public virtual returns (int32);
    // 获取账户余额，任何用户都可以调用
    function getAccountBalance() public view virtual returns (uint256);
    // 增加账户余额，内部接口，用户不可调用
    function addAccountBalance(uint256) public virtual returns (int32);
    // 减少账户余额，内部接口，用户不可调用
    function subAccountBalance(uint256) public virtual returns (int32);
}
