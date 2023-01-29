pragma solidity ^0.8.1;

contract ToBeDeploy {
    int m_value;

    function get() public view returns(int) {
        return m_value;
    }

    function set(int value) public {
        m_value = value;
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

contract HelloWorld {
    int m_intValue;
    string m_stringValue;
    event EventExample(int value1, string value2);

    function getInt() public view returns(int) {
        return m_intValue;
    }

    function setInt(int value) public {
        m_intValue = value;
    }

    function getString() public view returns(string memory) {
        return m_stringValue;
    }

    function setString(string memory value) public {
        m_stringValue = value;
    }

    function deployAndCall(int value) public returns(int) {
        ToBeDeploy contract2 = new ToBeDeploy();

        contract2.set(value);
        return contract2.get();
    }

    function returnRevert() public returns(int) {
        m_intValue = 1006;
        revert();
    }

    function returnRequire() public returns(int) {
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
}