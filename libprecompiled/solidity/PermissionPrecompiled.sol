pragma solidity ^0.4.24;

contract PermissionPrecompiled {
    function insert(string table_name, string addr) public returns (int256);
    function remove(string table_name, string addr) public returns (int256);
    function queryByName(string table_name) public view returns (string);

    function grantWrite(address contractAddr, address user)
        public
        returns (int256);
    function revokeWrite(address contractAddr, address user)
        public
        returns (int256);
    function queryPermission(address contractAddr) public view returns (string);
}
