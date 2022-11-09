#include "DelegateHostContext.h"
#include <bcos-utilities/AddressUtils.h>
using namespace bcos;
using namespace bcos::executor;

DelegateHostContext::DelegateHostContext(CallParameters::UniquePtr callParameters,
    std::shared_ptr<TransactionExecutive> executive, std::string tableName)
  : HostContext(std::move(callParameters), executive, tableName)
{
    if (!getCallParameters()->delegateCall)
    {
        EXECUTOR_LOG(FATAL) << "Construct a DelegateHostContext using non delegateCall params"
                            << getCallParameters()->toFullString();
        exit(1);
    }
    setCode(getCallParameters()->delegateCallCode);
    m_delegateCallSender = bcos::AddressUtils::padding(getCallParameters()->delegateCallSender);
}

std::optional<storage::Entry> DelegateHostContext::code()
{
    return m_code;
}

bool DelegateHostContext::setCode(bytes code)
{
    storage::Entry codeEntry;
    codeEntry.importFields({code});
    m_code = codeEntry;
    return true;
}

std::string_view DelegateHostContext::caller() const
{
    return m_delegateCallSender;
}