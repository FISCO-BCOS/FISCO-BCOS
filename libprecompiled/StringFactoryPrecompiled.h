#pragma once

#include "PrecompiledContext.h"
#include "StringPrecompiled.h"

namespace dev {

namespace precompiled {

#if 0
contract StringFactory {
    function newString(string) public returns(String);
    function newString(int) public returns(String);
    function newString(String) public returns(String);

    function newString(string, int begin, int end) public returns(String);
    function newString(String, int begin, int end) public returns(String);

    function toString() public constant returns(string);
}
{
    "b78f2cb3": "newString(address)",
    "b96d4ba0": "newString(address,int256,int256)",
    "cbea0803": "newString(int256)",
    "a316057e": "newString(string)",
    "a40fe5de": "newString(string,int256,int256)"
}

#endif

class StringFactoryPrecompiled: public Precompiled {
public:
	typedef std::shared_ptr<StringFactoryPrecompiled> Ptr;

	virtual ~StringFactoryPrecompiled() {};

	virtual void beforeBlock(PrecompiledContext::Ptr context);
	virtual void afterBlock(PrecompiledContext::Ptr context, bool commit);

	virtual std::string toString(PrecompiledContext::Ptr context);

	virtual bytes call(PrecompiledContext::Ptr context, bytesConstRef param);

	virtual Address newString(PrecompiledContext::Ptr context, const std::string &str);
};

}

}
