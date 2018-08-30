#pragma once

#include <libprecompiled/StringPrecompiled.h>
#include <libprecompiled/PrecompiledContext.h>
#include <libdevcore/Common.h>
#include "StateDB.h"

namespace dev {

namespace precompiled {

#if 0
contract Condition {
    function EQ(string, int);
    function EQ(string, string);
    function EQ(string, String);

    function NE(string, int);
    function NE(string, string);
    function NE(string, String);

    function GT(string, int);
    function GE(string, int);

    function LT(string, int);
    function LE(string, int);

    function limit(int);
    function limit(int, int);
}
{
    "c559a73a": "EQ(string,address)",
    "e44594b9": "EQ(string,int256)",
    "cd30a1d1": "EQ(string,string)",
    "42f8dd31": "GE(string,int256)",
    "08ad6333": "GT(string,int256)",
    "b6f23857": "LE(string,int256)",
    "c31c9b65": "LT(string,int256)",
    "9e029900": "NE(string,address)",
    "39aef024": "NE(string,int256)",
    "2783acf5": "NE(string,string)",
    "2e0d738a": "limit(int256)",
    "7ec1cc65": "limit(int256,int256)"
}
#endif

class ConditionPrecompiled: public Precompiled {
public:
	typedef std::shared_ptr<ConditionPrecompiled> Ptr;

	virtual ~ConditionPrecompiled() {};

	virtual void beforeBlock(std::shared_ptr<PrecompiledContext>);
	virtual void afterBlock(std::shared_ptr<PrecompiledContext>, bool commit);

	virtual std::string toString(std::shared_ptr<PrecompiledContext>);

	virtual bytes call(std::shared_ptr<PrecompiledContext> context, bytesConstRef param);

	void setPrecompiledEngine(PrecompiledContext::Ptr precompiledEngine) { _precompiledEngine = precompiledEngine; }

	void setCondition(dev::storage::Condition::Ptr condition) { _condition = condition; }
	dev::storage::Condition::Ptr getCondition() { return _condition; }

private:
	PrecompiledContext::Ptr _precompiledEngine;
	dev::storage::Condition::Ptr _condition;
};

}

}
