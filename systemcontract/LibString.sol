/**
* file LibString.sol
* author liaoyan
* time 2016-11-29
* desc the defination of LibString contract
*/

pragma solidity ^0.4.2;

library LibString {
    
    using LibString for *;
    
    function memcpy(uint dest, uint src, uint len) private {
        // Copy word-length chunks while possible
        for(; len >= 32; len -= 32) {
            assembly {
                mstore(dest, mload(src))
            }
            dest += 32;
            src += 32;
        }

        // Copy remaining bytes
        uint mask = 256 ** (32 - len) - 1;
        assembly {
            let srcpart := and(mload(src), not(mask))
            let destpart := and(mload(dest), mask)
            mstore(dest, or(destpart, srcpart))
        }
    }
    
    // Returns the memory address of the first byte of the first occurrence of
    // `needle` in `self`, or the first byte after `self` if not found.
    function findPtr(uint selflen, uint selfptr, uint needlelen, uint needleptr) private returns (uint) {
        uint ptr;
        uint idx;

        if (needlelen <= selflen) {
            if (needlelen <= 32) {
                // Optimized assembly for 68 gas per byte on short strings
                assembly {
                    let mask := not(sub(exp(2, mul(8, sub(32, needlelen))), 1))
                    let needledata := and(mload(needleptr), mask)
                    let end := add(selfptr, sub(selflen, needlelen))
                    ptr := selfptr
                    loop:
                    jumpi(exit, eq(and(mload(ptr), mask), needledata))
                    ptr := add(ptr, 1)
                    jumpi(loop, lt(sub(ptr, 1), end))
                    ptr := add(selfptr, selflen)
                    exit:
                }
                return ptr;
            } else {
                // For long needles, use hashing
                bytes32 hash;
                assembly { hash := sha3(needleptr, needlelen) }
                ptr = selfptr;
                for (idx = 0; idx <= selflen - needlelen; idx++) {
                    bytes32 testHash;
                    assembly { testHash := sha3(ptr, needlelen) }
                    if (hash == testHash)
                        return ptr;
                    ptr += 1;
                }
            }
        }
        return selfptr + selflen;
    }
    
    /*
    function length(string _self) internal returns (uint _ret) {
        _ret = bytes(_self).length;
    }
    */
    
    function compare(string _self, string _str) internal returns (int8 _ret) {
        for (uint i=0; i<bytes(_self).length && i<bytes(_str).length; ++i) {
            if (bytes(_self)[i] > bytes(_str)[i]) {
                return 1;
            } else if (bytes(_self)[i] < bytes(_str)[i]) {
                return -1;
            }
        }
        
        if (bytes(_self).length > bytes(_str).length) {
            return 1;
        } if (bytes(_self).length < bytes(_str).length) {
            return -1;
        } else {
            return 0;
        }
    }

    function compareNoCase(string _self, string _str) internal returns (int8 _ret) {
        for (uint i=0; i<bytes(_self).length && i<bytes(_str).length; ++i) {
            byte ch1 = bytes(_self)[i]|0x20;
            byte ch2 = bytes(_str)[i]|0x20;
            if (ch1 >= 'a' && ch1 <='z' && ch2 >= 'a' && ch2 <='z') {
                if (ch1 > ch2) {
                    return 1;
                } else if (ch1 < ch2) {
                    return -1;
                }
            } else {
                if (bytes(_self)[i] > bytes(_str)[i]) {
                    return 1;
                } else if (bytes(_self)[i] < bytes(_str)[i]) {
                    return -1;
                }
            }
        }
        
        if (bytes(_self).length > bytes(_str).length) {
            return 1;
        } if (bytes(_self).length < bytes(_str).length) {
            return -1;
        } else {
            return 0;
        }
    }

    function equals(string _self, string _str) internal returns (bool _ret) {
        if (bytes(_self).length != bytes(_str).length) {
            return false;
        }

        for (uint i=0; i<bytes(_self).length; ++i) {
            if (bytes(_self)[i] != bytes(_str)[i]) {
                return false;
            }
        }
        
        return true;
    }

    function equalsNoCase(string _self, string _str) internal returns (bool _ret) {
        if (bytes(_self).length != bytes(_str).length) {
            return false;
        }

        for (uint i=0; i<bytes(_self).length; ++i) {
            byte ch1 = bytes(_self)[i]|0x20;
            byte ch2 = bytes(_str)[i]|0x20;
            if (ch1 >= 'a' && ch1 <='z' && ch2 >= 'a' && ch2 <='z') {
                if (ch1 != ch2) {
                    return false;
                }
            } else {
                if (bytes(_self)[i] != bytes(_str)[i]) {
                    return false;
                }
            }
        }
        
        return true;
    }
    
    function substr(string _self, uint _start, uint _len) internal returns (string _ret) {
        if (_len > bytes(_self).length-_start) {
            _len = bytes(_self).length-_start;
        }

        if (_len <= 0) {
            _ret = "";
            return;
        }
        
        _ret = new string(_len);

        uint selfptr;
        uint retptr;
        assembly {
            selfptr := add(_self, 0x20)
            retptr := add(_ret, 0x20)
        }
        
        memcpy(retptr, selfptr+_start, _len);
    }
    
    function concat(string _self, string _str) internal returns (string _ret) {
        _ret = new string(bytes(_self).length + bytes(_str).length);

        uint selfptr;
        uint strptr;
        uint retptr;
        assembly {
            selfptr := add(_self, 0x20)
            strptr := add(_str, 0x20)
            retptr := add(_ret, 0x20)
        }
        
        memcpy(retptr, selfptr, bytes(_self).length);
        memcpy(retptr+bytes(_self).length, strptr, bytes(_str).length);
    }
    
    function concat(string _self, string _str1, string _str2)
        internal returns (string _ret) {
        _ret = new string(bytes(_self).length + bytes(_str1).length + bytes(_str2).length);

        uint selfptr;
        uint str1ptr;
        uint str2ptr;
        uint retptr;
        assembly {
            selfptr := add(_self, 0x20)
            str1ptr := add(_str1, 0x20)
            str2ptr := add(_str2, 0x20)
            retptr := add(_ret, 0x20)
        }
        
        uint pos = 0;
        memcpy(retptr+pos, selfptr, bytes(_self).length);
        pos += bytes(_self).length;
        memcpy(retptr+pos, str1ptr, bytes(_str1).length);
        pos += bytes(_str1).length;
        memcpy(retptr+pos, str2ptr, bytes(_str2).length);
        pos += bytes(_str2).length;
    }
    
    function concat(string _self, string _str1, string _str2, string _str3)
        internal returns (string _ret) {
        _ret = new string(bytes(_self).length + bytes(_str1).length + bytes(_str2).length
            + bytes(_str3).length);

        uint selfptr;
        uint str1ptr;
        uint str2ptr;
        uint str3ptr;
        uint retptr;
        assembly {
            selfptr := add(_self, 0x20)
            str1ptr := add(_str1, 0x20)
            str2ptr := add(_str2, 0x20)
            str3ptr := add(_str3, 0x20)
            retptr := add(_ret, 0x20)
        }
        
        uint pos = 0;
        memcpy(retptr+pos, selfptr, bytes(_self).length);
        pos += bytes(_self).length;
        memcpy(retptr+pos, str1ptr, bytes(_str1).length);
        pos += bytes(_str1).length;
        memcpy(retptr+pos, str2ptr, bytes(_str2).length);
        pos += bytes(_str2).length;
        memcpy(retptr+pos, str3ptr, bytes(_str3).length);
        pos += bytes(_str3).length;
    }
    
    function trim(string _self) internal returns (string _ret) {
        uint i;
        uint8 ch;
        for (i=0; i<bytes(_self).length; ++i) {
            ch = uint8(bytes(_self)[i]);
            if (!(ch == 0x20 || ch == 0x09 || ch == 0x0D || ch == 0x0A)) {
                break;
            }
        }
        uint start = i;
        
        for (i=bytes(_self).length; i>0; --i) {
            ch = uint8(bytes(_self)[i-1]);
            if (!(ch == 0x20 || ch == 0x09 || ch == 0x0D || ch == 0x0A)) {
                break;
            }
        }
        uint end = i;
        
        _ret = new string(end-start);
        
        uint selfptr;
        uint retptr;
        assembly {
            selfptr := add(_self, 0x20)
            retptr := add(_ret, 0x20)
        }
        
        memcpy(retptr, selfptr+start, end-start);
    }
    
    function trim(string _self, string _chars) internal returns (string _ret) {
        uint16 i;
        uint16 j;
        bool matched;
        for (i=0; i<bytes(_self).length; ++i) {
            matched = false;
            for (j=0; j<bytes(_chars).length; ++j) {
                if (bytes(_self)[i] == bytes(_chars)[j]) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                break;
            }
        }
        uint16 start = i;
        
        for (i=uint16(bytes(_self).length); i>0; --i) {
            matched = false;
            for (j=0; j<bytes(_chars).length; ++j) {
                if (bytes(_self)[i-1] == bytes(_chars)[j]) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                break;
            }
        }
        uint16 end = i;

        if (end <= start) {
            return;
        }
        
        _ret = new string(end-start);
        
        uint selfptr;
        uint retptr;
        assembly {
            selfptr := add(_self, 0x20)
            retptr := add(_ret, 0x20)
        }
        
        memcpy(retptr, selfptr+start, end-start);
    }
    
    function split(string _self, string _delim, string[] storage _array) internal {
        //Why can not use delete _array?
        for (uint i=0; i<_array.length; ++i) {
            delete _array[i];
        }
        _array.length = 0;

        uint selfptr;
        uint delimptr;
        assembly {
            selfptr := add(_self, 0x20)
            delimptr := add(_delim, 0x20)
        }
        
        uint pos = 0;
        while (true) {
            uint ptr;
            bool found = false;
            if (bytes(_delim).length > 0) {
                ptr = findPtr(bytes(_self).length-pos, selfptr+pos, bytes(_delim).length, delimptr) - selfptr;
                
                if (ptr < bytes(_self).length) {
                    found = true;
                } else {
                    ptr = bytes(_self).length;
                }
            } else {
                ptr = bytes(_self).length;
            }
            
            string memory elem = new string(ptr-pos);
            uint elemptr;
            assembly {
                elemptr := add(elem, 0x20)
            }
            memcpy(elemptr, selfptr+pos, ptr-pos);
            pos = ptr + bytes(_delim).length;
            _array.push(elem);
            
            if (!found) {
                break;
            }
        }
    }
    
    function indexOf(string _self, string _str) internal returns (int _ret) {
        uint selfptr;
        uint strptr;
        assembly {
            selfptr := add(_self, 0x20)
            strptr := add(_str, 0x20)
        }
        
        uint ptr = findPtr(bytes(_self).length, selfptr, bytes(_str).length, strptr) - selfptr;
        if (ptr < bytes(_self).length) {
            _ret = int(ptr);
        } else {
            _ret = -1;
        }
    }
    
    function indexOf(string _self, string _str, uint pos) internal returns (int _ret) {
        uint selfptr;
        uint strptr;
        assembly {
            selfptr := add(_self, 0x20)
            strptr := add(_str, 0x20)
        }
        
        uint ptr = findPtr(bytes(_self).length-pos, selfptr+pos, bytes(_str).length, strptr) - selfptr;
        if (ptr < bytes(_self).length) {
            _ret = int(ptr);
        } else {
            _ret = -1;
        }
    }
    
    function toInt(string _self) internal returns (int _ret) {
        _ret = 0;
        if (bytes(_self).length == 0) {
            return;
        }
        
        uint16 i;
        uint8 digit;
        for (i=0; i<bytes(_self).length; ++i) {
            digit = uint8(bytes(_self)[i]);
            if (!(digit == 0x20 || digit == 0x09 || digit == 0x0D || digit == 0x0A)) {
                break;
            }
        }
        
        bool positive = true;
        if (bytes(_self)[i] == '+') {
            positive = true;
            i++;
        } else if(bytes(_self)[i] == '-') {
            positive = false;
            i++;
        }

        for (; i<bytes(_self).length; ++i) {
            digit = uint8(bytes(_self)[i]);
            if (!(digit >= 0x30 && digit <= 0x39)) {
                return;
            }
            _ret = _ret*10 + int(digit-0x30);
        }        
        
        if (!positive) {
            _ret = -_ret;
        }
    }

    function toAddress(string _self) internal returns (address _ret) {
        uint16 i;
        uint8 digit;
        for (i=0; i<bytes(_self).length; ++i) {
            digit = uint8(bytes(_self)[i]);
            if (!(digit == 0x20 || digit == 0x09 || digit == 0x0D || digit == 0x0A)) {
                break;
            }
        }
        
        if (bytes(_self).length-i < 2) {
            return address(0);
        }

        //must start with 0x
        if (!(bytes(_self)[i] == '0' && bytes(_self)[i+1]|0x20 == 'x')) {
            return address(0);
        }

        uint addr = 0;
        
        for (i+=2; i<bytes(_self).length; ++i) {
            digit = uint8(bytes(_self)[i]);
            if (digit >= 0x30 && digit <= 0x39) //'0'-'9'
                digit -= 0x30;
            else if (digit|0x20 >= 0x61 && digit|0x20 <= 0x66) //'a'-'f'
                digit = digit-0x61+10;
            else 
                return address(0); 
            
            addr = addr*16+digit;
        }
        
        return address(addr);
    }
    
    function toKeyValue(string _self, string _key) internal returns (string _ret) {
        _ret = new string(bytes(_self).length + bytes(_key).length + 5);
        
        uint selfptr;
        uint keyptr;
        uint retptr;
        assembly {
            selfptr := add(_self, 0x20)
            keyptr := add(_key, 0x20)
            retptr := add(_ret, 0x20)
        }
        
        uint pos = 0;

        bytes(_ret)[pos++] = '"';
        memcpy(retptr+pos, keyptr, bytes(_key).length);
        pos += bytes(_key).length;
        bytes(_ret)[pos++] = '"';
        
        bytes(_ret)[pos++] = ':';
        
        bytes(_ret)[pos++] = '"';
        memcpy(retptr+pos, selfptr, bytes(_self).length);
        pos += bytes(_self).length;
        bytes(_ret)[pos++] = '"';
    }

    function toKeyValue(string[] storage _self, string _key) internal returns (string _ret) {
        uint len = bytes(_key).length+5;
        for (uint i=0; i<_self.length; ++i) {
            if (i < _self.length-1)
                len += bytes(_self[i]).length+3;
            else
                len += bytes(_self[i]).length+2;
        }

        _ret = new string(len);

        uint pos = 0;

        bytes(_ret)[pos++] = '"';
        for (uint j=0; j<bytes(_key).length; ++j) {
            bytes(_ret)[pos++] = bytes(_key)[j];
        }
        bytes(_ret)[pos++] = '"';

        bytes(_ret)[pos++] = ':';

        bytes(_ret)[pos++] = '[';
        
        for (i=0; i<_self.length; ++i) {
            bytes(_ret)[pos++] = '"';
            for (j=0; j<bytes(_self[i]).length; ++j) {
                bytes(_ret)[pos++] = bytes(_self[i])[j];
            }
            bytes(_ret)[pos++] = '"';

            if (i < _self.length-1)
                bytes(_ret)[pos++] = ',';
        }
        
        bytes(_ret)[pos++] = ']';
    }
    
    function getStringValueByKey(string _self, string _key) internal returns (string _ret) {
        int pos = -1;
        uint searchStart = 0;
        while (true) {
            pos = _self.indexOf("\"".concat(_key, "\""), searchStart);
            if (pos == -1) {
                pos = _self.indexOf("'".concat(_key, "'"), searchStart);
                if (pos == -1) {
                    return;
                }
            }

            pos += int(bytes(_key).length+2);

            bool colon = false;
            while (uint(pos) < bytes(_self).length) {
                if (bytes(_self)[uint(pos)] == ' ' || bytes(_self)[uint(pos)] == '\t' 
                    || bytes(_self)[uint(pos)] == '\r' || bytes(_self)[uint(pos)] == '\n') {
                    pos++;
                } else if (bytes(_self)[uint(pos)] == ':') {
                    pos++;
                    colon = true;
                    break;
                } else {
                    break;
                }
            }

            if(uint(pos) == bytes(_self).length) {
                return;
            }

            if (colon) {
                break;
            } else {
                searchStart = uint(pos);
            }
        }
        
        bool doubleQuotes = true;
        int start = _self.indexOf("\"", uint(pos));
        if (start == -1) {
            doubleQuotes = false;
            start = _self.indexOf("'", uint(pos));
            if (start == -1) {
                return;
            }
        }
        start += 1;
        
        int end;
        if (doubleQuotes) {
            end = _self.indexOf("\"", uint(start));
        } else {
            end = _self.indexOf("'", uint(start));
        }
        if (end == -1) {
            return;
        }
        
        _ret = _self.substr(uint(start), uint(end-start));
    }
    
    function getIntValueByKey(string _self, string _key) internal returns (int _ret) {
        _ret = 0;
        int pos = -1;
        uint searchStart = 0;
        while (true) {
            pos = _self.indexOf("\"".concat(_key, "\""), searchStart);
            if (pos == -1) {
                pos = _self.indexOf("'".concat(_key, "'"), searchStart);
                if (pos == -1) {
                    return;
                }
            }

            pos += int(bytes(_key).length+2);

            bool colon = false;
            while (uint(pos) < bytes(_self).length) {
                if (bytes(_self)[uint(pos)] == ' ' || bytes(_self)[uint(pos)] == '\t' 
                    || bytes(_self)[uint(pos)] == '\r' || bytes(_self)[uint(pos)] == '\n') {
                    pos++;
                } else if (bytes(_self)[uint(pos)] == ':') {
                    pos++;
                    colon = true;
                    break;
                } else {
                    break;
                }
            }

            if(uint(pos) == bytes(_self).length) {
                return;
            }

            if (colon) {
                break;
            } else {
                searchStart = uint(pos);
            }
        }

        uint i = uint(pos);
        uint8 digit;
        for (; i<bytes(_self).length; ++i) {
            digit = uint8(bytes(_self)[i]);
            if (!(digit == 0x20 || digit == 0x09 || digit == 0x0D || digit == 0x0A 
            || digit == 0x3A /*:*/ || digit == 0x22 /*"*/ || digit == 0x27 /*'*/)) {
                break;
            }
        }
        
        bool positive = true;
        if (bytes(_self)[i] == '+') {
            positive = true;
            i++;
        } else if(bytes(_self)[i] == '-') {
            positive = false;
            i++;
        }

        for (; i<bytes(_self).length; ++i) {
            digit = uint8(bytes(_self)[i]);
            if (!(digit >= 0x30 && digit <= 0x39)) {
                if (!positive) {
                    _ret = -_ret;
                }
                return;
            }
            _ret = _ret*10 + int(digit-0x30);
        }        
        
        if (!positive) {
            _ret = -_ret;
        }
    }
    
    function getArrayValueByKey(string _self, string _key) internal returns (string _ret) {
        int pos = -1;
        uint searchStart = 0;
        while (true) {
            pos = _self.indexOf("\"".concat(_key, "\""), searchStart);
            if (pos == -1) {
                pos = _self.indexOf("'".concat(_key, "'"), searchStart);
                if (pos == -1) {
                    return;
                }
            }

            pos += int(bytes(_key).length+2);

            bool colon = false;
            while (uint(pos) < bytes(_self).length) {
                if (bytes(_self)[uint(pos)] == ' ' || bytes(_self)[uint(pos)] == '\t' 
                    || bytes(_self)[uint(pos)] == '\r' || bytes(_self)[uint(pos)] == '\n') {
                    pos++;
                } else if (bytes(_self)[uint(pos)] == ':') {
                    pos++;
                    colon = true;
                    break;
                } else {
                    break;
                }
            }

            if(uint(pos) == bytes(_self).length) {
                return;
            }

            if (colon) {
                break;
            } else {
                searchStart = uint(pos);
            }
        }

        int start = _self.indexOf("[", uint(pos));
        if (start == -1) {
            return;
        }
        start += 1;
        
        int end = _self.indexOf("]", uint(pos));
        if (end == -1) {
            return;
        }
        
        _ret = _self.substr(uint(start), uint(end-start));
    }

    function getArrayValueByKey(string _self, string _key, string[] storage _array) internal {
        //Why can not use delete _array?
        for (uint i=0; i<_array.length; ++i) {
            delete _array[i];
        }
        _array.length = 0;

        int pos = -1;
        uint searchStart = 0;
        while (true) {
            pos = _self.indexOf("\"".concat(_key, "\""), searchStart);
            if (pos == -1) {
                pos = _self.indexOf("'".concat(_key, "'"), searchStart);
                if (pos == -1) {
                    return;
                }
            }

            pos += int(bytes(_key).length+2);

            bool colon = false;
            while (uint(pos) < bytes(_self).length) {
                if (bytes(_self)[uint(pos)] == ' ' || bytes(_self)[uint(pos)] == '\t' 
                    || bytes(_self)[uint(pos)] == '\r' || bytes(_self)[uint(pos)] == '\n') {
                    pos++;
                } else if (bytes(_self)[uint(pos)] == ':') {
                    pos++;
                    colon = true;
                    break;
                } else {
                    break;
                }
            }

            if(uint(pos) == bytes(_self).length) {
                return;
            }

            if (colon) {
                break;
            } else {
                searchStart = uint(pos);
            }
        }

        int start = _self.indexOf("[", uint(pos));
        if (start == -1) {
            return;
        }
        start += 1;
        
        int end = _self.indexOf("]", uint(pos));
        if (end == -1) {
            return;
        }

        string memory vals = _self.substr(uint(start), uint(end-start)).trim(" \t\r\n");

        if (bytes(vals).length == 0) {
            return;
        }
        
        vals.split(",", _array);

        for (i=0; i<_array.length; ++i) {
            _array[i] = _array[i].trim(" \t\r\n");
            _array[i] = _array[i].trim("'\"");
        }
    }

    function keyExists(string _self, string _key) internal returns (bool _ret) {
        int pos = _self.indexOf("\"".concat(_key, "\""));
        if (pos == -1) {
            pos = _self.indexOf("'".concat(_key, "'"));
            if (pos == -1) {
                return false;
            }
        }

        return true;
    }

    function storageToUint(string _self) internal returns (uint _ret) {
        uint len = bytes(_self).length;
        if (len > 32) {
            len = 32;
        }
        
        _ret = 0;
        for(uint i=0; i<len; ++i) {
            _ret = _ret*256 + uint8(bytes(_self)[i]);
        }
    }

    function inArray(string _self, string[] storage _array) internal returns (bool _ret) {
        for (uint i=0; i<_array.length; ++i) {
            if (_self.equals(_array[i])) {
                return true;
            }
        }

        return false;
    }
 
    function inArrayNoCase(string _self, string[] storage _array) internal returns (bool _ret) {
        for (uint i=0; i<_array.length; ++i) {
            if (_self.equalsNoCase(_array[i])) {
                return true;
            }
        }

        return false;
    }
    
    function addrToAsciiString(address x) internal returns (string) {
        bytes memory s = new bytes(40);
        for (uint i = 0; i < 20; i++) {
            byte b = byte(uint8(uint(x) / (2**(8*(19 - i)))));
            byte hi = byte(uint8(b) / 16);
            byte lo = byte(uint8(b) - 16 * uint8(hi));
            s[2*i] = toChar(hi);
            s[2*i+1] = toChar(lo);            
        }
        return string(s);
    }

    function toChar(byte b) internal returns (byte c) {
        if (b < 10) return byte(uint8(b) + 0x30);
        else return byte(uint8(b) + 0x57);
    }

    function toUpper(string _self) internal returns (string _ret) {
        for (uint i=0; i<bytes(_self).length; ++i) {
            if (bytes(_self)[i] >= 'a' && bytes(_self)[i] <= 'z') {
                bytes(_self)[i] &= ~0x20;
            }
        }
        
        _ret = _self;
    }
    
    function toLower(string _self) internal returns (string _ret) {
        for (uint i=0; i<bytes(_self).length; ++i) {
            if (bytes(_self)[i] >= 'A' && bytes(_self)[i] <= 'Z') {
                bytes(_self)[i] |= 0x20;
            }
        }
        
        _ret = _self;
    }
  }