
pragma solidity ^0.6.0;

contract B {

    event LogCreateB(int value);
    event LogIncrement(int value);
    constructor(int value) public {
        m_value = value;
        emit LogCreateB(value);
    }
    
    function value() public view returns(int) {
        return m_value;
    }
    
    function incValue() public {
        ++m_value;
        emit LogIncrement(m_value);
    }
    
    int m_value;
}

contract A {
    B b;
    event LogCallB(int value);
    function createAndCallB (int amount) public returns(int) {
        b = new B(amount);
        emit LogCallB(amount);
        return b.value();
    }
}
