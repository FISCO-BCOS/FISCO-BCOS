#pragma once
#include "libprecompiled/PrecompiledContext.h"

namespace dev
{

namespace storage
{
class DB;
}

namespace precompiled
{
class CRUDPrecompiled : public Precompiled
{
public:
    typedef std::shared_ptr<CRUDPrecompiled> Ptr;

    virtual ~CRUDPrecompiled(){};

    virtual void beforeBlock(PrecompiledContext::Ptr);
    virtual void afterBlock(PrecompiledContext::Ptr, bool commit);

    virtual std::string toString(PrecompiledContext::Ptr);

    virtual bytes call(PrecompiledContext::Ptr context, bytesConstRef param);
protected:
    std::shared_ptr<storage::DB> openTable(PrecompiledContext::Ptr context, const std::string& tableName);
};

}  // namespace precompiled

}  // namespace dev
