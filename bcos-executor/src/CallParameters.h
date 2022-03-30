#pragma once

#include "bcos-protocol/LogEntry.h"
#include <bcos-utilities/Common.h>
#include <memory>
#include <string>

namespace bcos::executor
{
struct CallParameters
{
    using UniquePtr = std::unique_ptr<CallParameters>;
    using UniqueConstPtr = std::unique_ptr<const CallParameters>;

    enum Type : int8_t
    {
        MESSAGE = 0,
        KEY_LOCK,
        FINISHED,
        REVERT,
    };

    explicit CallParameters(Type _type) : type(_type) {}

    CallParameters(const CallParameters&) = delete;
    CallParameters& operator=(const CallParameters&) = delete;

    CallParameters(CallParameters&&) = delete;
    CallParameters(const CallParameters&&) = delete;

    int64_t contextID = 0;
    int64_t seq = 0;

    std::string senderAddress;   // common field, readable format
    std::string codeAddress;     // common field, readable format
    std::string receiveAddress;  // common field, readable format
    std::string origin;          // common field, readable format

    int64_t gas = 0;             // common field
    bcos::bytes data;            // common field, transaction data, binary format
    std::string abi;             // common field, contract abi, json format

    std::vector<std::string> keyLocks;  // common field
    std::string acquireKeyLock;         // by response

    std::string message;                               // by response, readable format
    std::vector<bcos::protocol::LogEntry> logEntries;  // by response
    std::optional<u256> createSalt;                    // by response
    std::string newEVMContractAddress;                 // by response, readable format

    int32_t status = 0;  // by response
    Type type;
    bool staticCall = false;  // common field
    bool create = false;      // by request, is create
};
}  // namespace bcos::executor