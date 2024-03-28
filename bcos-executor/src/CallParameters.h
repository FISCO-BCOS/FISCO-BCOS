#pragma once

#include <bcos-framework/protocol/LogEntry.h>
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
        KEY_LOCK = 1,
        FINISHED = 2,
        REVERT = 3,
        PRE_FINISH = 4,
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

    /// WARNING: gasLeft, be cautious to assign value
    int64_t gas = 0;   // common field
    bcos::bytes data;  // common field, transaction data, binary format
    std::string abi;   // common field, contract abi, json format

    // balance
    u256 value = 0;
    u256 gasPrice = 0;
    u256 effectiveGasPrice = 0;
    int64_t gasLimit = 0;
    u256 maxFeePerGas = 0;
    u256 maxPriorityFeePerGas = 0;

    std::vector<std::string> keyLocks;  // common field
    std::string acquireKeyLock;         // by response

    std::string message;                               // by response, readable format
    std::vector<bcos::protocol::LogEntry> logEntries;  // by response
    std::optional<u256> createSalt;                    // by response
    std::string newEVMContractAddress;                 // by response, readable format

    int32_t status = 0;  // by response
    int32_t evmStatus = 0;
    Type type;
    bool staticCall = false;      // common field
    bool create = false;          // by request, is creation
    bool internalCreate = false;  // by internal precompiled request, is creation
    /**
     * Internal precompiled contract request, this option is used to
     * modify contract table, which address scheduled by 'to', by a
     * certain precompiled contract
     */
    bool internalCall = false;

    // delegateCall
    bool delegateCall = false;
    bytes delegateCallCode;
    h256 delegateCallCodeHash;
    std::string delegateCallSender;
    bool hasContractTableChanged = false;

    std::string toString()
    {
        std::stringstream ss;
        ss << "[" << contextID << "|" << seq << "|";
        switch (type)
        {
        case MESSAGE:
            ss << "MESSAGE";
            break;
        case KEY_LOCK:
            ss << "KEY_LOCK";
            break;
        case FINISHED:
            ss << "FINISHED";
            break;
        case REVERT:
            ss << "REVERT";
            break;
        case PRE_FINISH:
            ss << "PRE_FINISH";
            break;
        };
        ss << "]";
        return ss.str();
    }

    // this method only for trace log
    std::string toFullString()
    {
        std::stringstream ss;
        // clang-format off
        ss << toString()
           << "senderAddress:" << senderAddress << "|"
           << "codeAddress:" << codeAddress << "|"
           << "receiveAddress:" << receiveAddress << "|"
           << "origin:" << origin << "|"
           << "gas:" << gas << "|"
           << "value:" << value << "|"
           << "dataSize:" << data.size() << "|"
           << "abiSize:" << abi.size() << "|"
           << "acquireKeyLock:" << acquireKeyLock << "|"
           << "message:" << message << "|"
           << "newEVMContractAddress:" << newEVMContractAddress << "|"
           << "staticCall:" << staticCall << "|"
           << "create :" << create << "|"
           << "delegateCall:" << delegateCall << "|"
           << "delegateCallSender" << delegateCallSender  << "|"
            << "hasContractTableChanged" << hasContractTableChanged;
        // clang-format on
        ss << "|logEntries: ";
        for (const auto& logEntry : logEntries)
        {
            ss << "[" << logEntry.address() << "|"
               << toHexStringWithPrefix(
                      h256((byte*)logEntry.topics().data(), logEntry.topics().size()))
               << "|" << toHexStringWithPrefix(logEntry.data()) << "]";
        }
        return ss.str();
    }
};
}  // namespace bcos::executor