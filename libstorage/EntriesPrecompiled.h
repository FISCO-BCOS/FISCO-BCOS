#pragma once

#include <libprecompiled/StringPrecompiled.h>
#include <libprecompiled/StringFactoryPrecompiled.h>
#include "../libprecompiled/PrecompiledContext.h"
#include "DB.h"

namespace dev {

namespace precompiled {

#if 0
contract Entries {
    function get(int) public constant returns(Entry);
    function size() public constant returns(int);
}
{
    "846719e0": "get(int256)",
    "949d225d": "size()"
}
#endif

class EntriesPrecompiled: public Precompiled {
public:
	typedef std::shared_ptr<EntriesPrecompiled> Ptr;

	virtual ~EntriesPrecompiled() {};

	virtual void beforeBlock(std::shared_ptr<PrecompiledContext>);
	virtual void afterBlock(std::shared_ptr<PrecompiledContext>, bool commit);

	virtual std::string toString(std::shared_ptr<PrecompiledContext>);

	virtual bytes call(std::shared_ptr<PrecompiledContext> context, bytesConstRef param);

	void setStringFactoryPrecompiled(StringFactoryPrecompiled::Ptr stringFactoryPrecompiled) { _stringFactoryPrecompiled = stringFactoryPrecompiled; }

	void setEntries(dev::storage::Entries::Ptr entries) { _entries = entries; }
	dev::storage::Entries::Ptr getEntries() { return _entries; }

private:
	dev::storage::Entries::Ptr _entries;
	StringFactoryPrecompiled::Ptr _stringFactoryPrecompiled;
};

}

}
