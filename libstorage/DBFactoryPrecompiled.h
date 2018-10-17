#pragma once

#include <libprecompiled/PrecompiledContext.h>
#include <libdevcrypto/Common.h>
#include <map>
#include "DBPrecompiled.h"
#include "MemoryDBFactory.h"

namespace dev {

namespace precompiled {

	const unsigned TABLE_NOT_EXISTS = 0;
	const unsigned TABLE_ALREADY_OPEN = 1;
	const unsigned TABLENAME_ALREADY_EXISTS = 2;
	const unsigned TABLENAME_CONFLICT = 101;

#if 0
{
    "56004b6a": "createTable(string,string,string)",
    "c184e0ff": "openDB(string)",
    "f23f63c9": "openTable(string)"
}
contract DBFactory {
    function openDB(string) public constant returns (DB);
    function openTable(string) public constant returns (DB);
    function createTable(string, string, string) public constant returns (DB);
}
#endif

class DBFactoryPrecompiled: public Precompiled {
public:
	typedef std::shared_ptr<DBFactoryPrecompiled> Ptr;
	virtual ~DBFactoryPrecompiled() {};

	virtual void beforeBlock(std::shared_ptr<PrecompiledContext>);
	virtual void afterBlock(std::shared_ptr<PrecompiledContext> context, bool commit);

	virtual std::string toString(std::shared_ptr<PrecompiledContext>);

	virtual bytes call(std::shared_ptr<PrecompiledContext> context, bytesConstRef param);

	void setMemoryDBFactory(dev::storage::MemoryDBFactory::Ptr memoryDBFactory) { _memoryDBFactory = memoryDBFactory; }

	h256 hash(std::shared_ptr<PrecompiledContext> context);

	Address openTable(PrecompiledContext::Ptr context, const std::string & tableName);
private:
	unsigned isTableCreated(PrecompiledContext::Ptr context, const std::string& tableName, const std::string &keyField, const std::string &valueFiled);
	dev::storage::MemoryDBFactory::Ptr _memoryDBFactory;
	std::map<std::string, Address> _name2Table;
	h256 _hash;
};

}

}
