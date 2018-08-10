pragma solidity ^0.4.4;

//abi info base contract
contract ContractBase{
    string version; //contract version info ,can be empty
    //constrct function
    function ContractBase(string version_para) {
        version = version_para;
    }
    //set member version
    function setVersion(string version_para) public {
        version = version_para;
    }
    //get member version
    function getVersion() constant public returns(string) {
        return version;
    }
}