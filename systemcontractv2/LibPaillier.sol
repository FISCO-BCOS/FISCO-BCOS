pragma solidity ^0.4.2;

import "LibString.sol";
import "LibInt.sol";

library LibPaillier {
    using LibString for *;
    using LibInt for *;
    
    function pai_add(string d1, string d2) internal constant returns (string) {
        string memory cmd = "[69d98d6a04c41b4605aacb7bd2f74bee][08paillier]";
        cmd = cmd.concat("|", d1);
        cmd = cmd.concat("|", d2);

        uint rLen = bytes(d1).length;
        string memory result = new string(rLen);

        uint strptr;
        assembly {
            strptr := add(result, 0x20)
        }
        cmd = cmd.concat("|", strptr.toString());

        bytes32 hash;
        uint strlen = bytes(cmd).length;
        assembly {
            strptr := add(cmd, 0x20)
            hash := sha3(strptr, strlen)
        }

        string memory errRet = "";
        uint ret = uint(hash);
        if (ret != 0) {
            return errRet;
        }
        
        return result;
    }
}
