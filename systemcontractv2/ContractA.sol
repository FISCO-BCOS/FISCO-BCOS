pragma solidity ^0.4.4;

contract ContractA{
    string content;
    function ContractA() public {
       content="Init";
    }
    function get() public constant returns(string){
        return content;
    }
    function set1(string n) public {
        content=n;
    }
    function set2(string n) public {
        set4(n);
    }
    function set3(string n) public {
        content=n;
    }
    function set4(string n) private {
        content=n;
    }
}
