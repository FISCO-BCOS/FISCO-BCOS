#pragma once
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/executor/ExecutionMessage.h>
#include <bcos-framework/interfaces/protocol/TransactionReceiptFactory.h>
#include <bcos-utilities/Error.h>
#include <sstream>
#include <stack>

namespace bcos::scheduler
{

struct ExecutiveState  // Executive state per tx
{
    using Ptr = std::shared_ptr<ExecutiveState>;
    ExecutiveState(
        int64_t _contextID, bcos::protocol::ExecutionMessage::UniquePtr _message, bool _enableDAG)
      : contextID(_contextID), message(std::move(_message)), enableDAG(_enableDAG), id(_contextID)
    {}

    int64_t contextID;
    std::stack<int64_t, std::list<int64_t>> callStack;
    bcos::protocol::ExecutionMessage::UniquePtr message;
    bcos::Error::UniquePtr error;
    int64_t currentSeq = 0;
    bool enableDAG;
    bool skip = false;
    int64_t id;

    std::string toString()
    {
        std::stringstream ss;
        ss << " " << contextID << " | " << callStack.size() << " | ["
           << (message ? message->toString() : "null") << "]";
        return ss.str();
    }
};

using ExecutiveStates = std::shared_ptr<std::vector<ExecutiveState::Ptr>>;

struct ExecutiveResult
{
    using Ptr = std::shared_ptr<ExecutiveResult>;
    bcos::protocol::TransactionReceipt::Ptr receipt;
    bcos::crypto::HashType transactionHash;
    std::string source;
};
}  // namespace bcos::scheduler
