#pragma once
#include "Precompiled.h"
#include "PrecompiledContext.h"
#include "libstorage/MemoryStateDBFactory.h"
#include <libdevcore/Common.h>

namespace dev
{
namespace precompiled
{
class CRUDPrecompiled : public Precompiled
{
public:
    typedef std::shared_ptr<CRUDPrecompiled> Ptr;

    CRUDPrecompiled() : _memoryDBFactory(nullptr){};
    virtual ~CRUDPrecompiled(){};

    virtual void beforeBlock(std::shared_ptr<PrecompiledContext>);
    virtual void afterBlock(std::shared_ptr<PrecompiledContext>, bool commit);

    virtual std::string toString(std::shared_ptr<PrecompiledContext>);

    virtual bytes call(std::shared_ptr<PrecompiledContext> context, bytesConstRef param);
    void setMemoryDBFactory(dev::storage::MemoryStateDBFactory::Ptr memoryDBFactory)
    {
        _memoryDBFactory = memoryDBFactory;
    }
	h256 hash(std::shared_ptr<PrecompiledContext> context);

private:
    dev::storage::DB::Ptr openTable(std::shared_ptr<PrecompiledContext> context, const std::string& tableName);

    dev::storage::MemoryStateDBFactory::Ptr _memoryDBFactory;
    std::map<std::string, dev::storage::DB::Ptr> _name2Table;
    h256 _hash;
};

}  // namespace precompiled

}  // namespace dev
