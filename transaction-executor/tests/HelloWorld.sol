// SPDX-License-Identifier: MIT
pragma solidity ^0.8.1;

contract ToBeDeploy {
    int m_value;

    function get() public view returns (int) {
        return m_value;
    }

    function set(int value) public {
        m_value = value;
    }
}

contract ToBeDeployWithMapping {
    mapping(address => int) public address2Int;
    address[] public addresses;

    constructor() {
        for (int i = 0; i < 10; ++i) {
            address addr = address(new ToBeDeploy());
            address2Int[addr] = i;
            addresses.push(addr);
        }
    }

    function all() public view returns (address[] memory) {
        return addresses;
    }

    function get(address addr) public view returns (int) {
        return address2Int[addr];
    }
}

contract DelegateCallTarget {
    int m_aValue;
    string m_bValue;

    function callMe() public {
        m_aValue = 19876;
        m_bValue = "hi!";
    }
}

contract DeployWithDeploy {
    address m_deployedAddress;

    constructor() {
        m_deployedAddress = address(new ToBeDeployWithMapping());
    }

    function getAddress() public view returns (address) {
        return m_deployedAddress;
    }
}

contract HelloWorld {
    int m_intValue;
    string m_stringValue;
    mapping(address => int) m_accounts;

    event EventExample(int value1, string value2);

    function balance(address to) public view returns (int) {
        return m_accounts[to];
    }

    function issue(address to, int count) public {
        m_accounts[to] += count;
    }

    function transfer(address from, address to, int count) public {
        m_accounts[from] -= count;
        m_accounts[to] += count;
    }

    function getInt() public view returns (int) {
        return m_intValue;
    }

    function setInt(int value) public {
        m_intValue = value;
    }

    function getString() public view returns (string memory) {
        return m_stringValue;
    }

    function setString(string memory value) public {
        m_stringValue = value;
    }

    function deployAndCall(int value) public returns (int) {
        ToBeDeploy contract2 = new ToBeDeploy();

        contract2.set(value);
        return contract2.get();
    }

    function returnRevert() public returns (int) {
        m_intValue = 1006;
        revert();
    }

    function returnRequire() public returns (int) {
        m_intValue = 1005;
        require(false);
    }

    function delegateCall() public {
        DelegateCallTarget target = new DelegateCallTarget();
        address addr = address(target);

        bool success;
        bytes memory ret;
        (success, ret) = addr.delegatecall(abi.encodeWithSignature("callMe()"));
    }

    function logOut() public {
        emit EventExample(m_intValue, m_stringValue);
    }

    function createTwice() public {
        ToBeDeploy contract1 = new ToBeDeploy();
        ToBeDeploy contract2 = new ToBeDeploy();

        require(contract1 != contract2);
    }

    function deployWithDeploy() public returns (address) {
        return address(new ToBeDeployWithMapping());
    }
}
