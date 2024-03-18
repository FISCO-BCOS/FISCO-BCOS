#include "DelegateHostContext.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-task/Wait.h"
#include <utility>
using namespace bcos;
using namespace bcos::executor;

DelegateHostContext::DelegateHostContext(CallParameters::UniquePtr callParameters,
    std::shared_ptr<TransactionExecutive> executive, std::string tableName)
  : HostContext(std::move(callParameters), std::move(executive), std::move(tableName))
{
    if (!getCallParameters()->delegateCall)
    {
        EXECUTOR_LOG(FATAL) << "Construct a DelegateHostContext using non delegateCall params"
                            << getCallParameters()->toFullString();
        exit(1);
    }
    setCode(getCallParameters()->delegateCallCode);
    m_delegateCallSender = getCallParameters()->delegateCallSender;
    m_thisAddress = getCallParameters()->receiveAddress;
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

    task::syncWait([&]() -> task::Task<void> {
        auto codeHashEntry = co_await storage2::readOne(executive().storage().storage(),
            transaction_executor::StateKeyView(getTableName(), "codeHash"));
        if (codeHashEntry)
        {
            auto view = codeHashEntry->get();
            m_codeHash = h256((const uint8_t*)view.data(), view.size());
        }
        else
        {
            m_codeHash = hashImpl()->hash(code);
        }
    }());

    return true;
}

h256 DelegateHostContext::codeHash()
{
    return m_codeHash;
}

std::string_view DelegateHostContext::myAddress() const
{
    if (this->features().get(
            ledger::Features::Flag::bugfix_evm_create2_delegatecall_staticcall_codecopy))
    {
        return m_thisAddress;
    }

    return HostContext::myAddress();
}

std::string_view DelegateHostContext::caller() const
{
    return m_delegateCallSender;
}