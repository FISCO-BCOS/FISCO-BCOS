#pragma once
#include "Precompiled.h"
#include "PrecompiledContext.h"

#include <libdevcore/Common.h>

namespace dev {

namespace precompiled {

#if 0
contract String {
    function charAt(int) public constant returns (bytes1);

    function equal(String) public constant returns (bool);
    function equal(string) public constant returns (bool);

    function concat(String) public returns (String);
    function concat(string) public returns (String);
    function concat(byte) public returns (String);

    function contains(String) public;
    function contains(string) public;
    function contains(byte) public;

    function isEmpty() public constant returns(bool);

    function length() public constant returns(int);

    function toInt() public constant returns(int);
    function toString() public constant returns(string);
}
{
    "1d730734": "charAt(int256)",
    "2214419b": "concat(address)",
    "f632a809": "concat(bytes1)",
    "7881c417": "concat(string)",
    "5dbe47e8": "contains(address)",
    "d556a894": "contains(bytes1)",
    "6833d54f": "contains(string)",
    "b9737cbe": "equal(address)",
    "7f241421": "equal(string)",
    "681fe70c": "isEmpty()",
    "1f7b6d32": "length()",
    "a833386b": "toBytes32()",
    "3f96f3a3": "toInt()",
    "47e46806": "toString()"
}

#endif

class StringPrecompiled: public Precompiled {
public:
	typedef std::shared_ptr<StringPrecompiled> Ptr;

	StringPrecompiled(const std::string &str): _str(str) {};
	virtual ~StringPrecompiled() {};

	virtual void beforeBlock(std::shared_ptr<PrecompiledContext>);
	virtual void afterBlock(std::shared_ptr<PrecompiledContext>, bool commit);

	virtual std::string toString(std::shared_ptr<PrecompiledContext>);

	virtual bytes call(std::shared_ptr<PrecompiledContext> context, bytesConstRef param);

private:
	std::string _str;
};

}

}
