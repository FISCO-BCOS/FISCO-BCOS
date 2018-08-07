pragma solidity ^0.4.2;
contract CastTest{
        function cast(){
         uint8[] memory a = new uint8[](1);
         uint256 b;
         a[0] = 0x34;
         b = uint256(a[0]);
        }
}
