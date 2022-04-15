#pragma once

#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include <boost/iterator/transform_iterator.hpp>
#include <boost/multi_index/detail/modify_key_adaptor.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/range/iterator_range_core.hpp>

namespace bcos::executor
{
class NativeExecutionMessage : public protocol::ExecutionMessage
{
public:
    NativeExecutionMessage() = default;
    virtual ~NativeExecutionMessage() {}

    Type type() const override { return m_type; }
    void setType(Type type) override { m_type = type; }

    crypto::HashType transactionHash() const override { return m_transactionHash; }
    void setTransactionHash(crypto::HashType hash) override { m_transactionHash = hash; }

    int64_t contextID() const override { return m_contextID; }
    void setContextID(int64_t contextID) override { m_contextID = contextID; }

    int64_t seq() const override { return m_seq; }
    void setSeq(int64_t seq) override { m_seq = seq; }

    std::string_view origin() const override { return m_origin; }
    void setOrigin(std::string origin) override { m_origin = std::move(origin); }

    std::string_view from() const override { return m_from; }
    void setFrom(std::string from) override { m_from = std::move(from); }

    std::string_view to() const override { return m_to; }
    void setTo(std::string to) override { m_to = std::move(to); }

    int32_t depth() const override { return m_depth; }
    void setDepth(int32_t depth) override { m_depth = depth; }

    bool create() const override { return m_create; }
    void setCreate(bool create) override { m_create = create; }

    int64_t gasAvailable() const override { return m_gasAvailable; }
    void setGasAvailable(int64_t gasAvailable) override { m_gasAvailable = gasAvailable; }

    bcos::bytesConstRef data() const override { return ref(m_data); }
    bcos::bytes takeData() override { return std::move(m_data); }
    void setData(bcos::bytes input) override { m_data = std::move(input); }

    bool staticCall() const override { return m_staticCall; }
    void setStaticCall(bool staticCall) override { m_staticCall = staticCall; }

    std::optional<u256> createSalt() const override { return m_createSalt; }
    void setCreateSalt(u256 createSalt) override { m_createSalt = createSalt; }

    int32_t status() const override { return m_status; }
    void setStatus(int32_t status) override { m_status = status; }

    std::string_view message() const override { return m_message; }
    void setMessage(std::string message) override { m_message = std::move(message); }

    gsl::span<bcos::protocol::LogEntry const> const logEntries() const override
    {
        return m_logEntries;
    }
    std::vector<bcos::protocol::LogEntry> takeLogEntries() override
    {
        return std::move(m_logEntries);
    }
    void setLogEntries(std::vector<bcos::protocol::LogEntry> logEntries) override
    {
        m_logEntries = std::move(logEntries);
    }

    std::string_view newEVMContractAddress() const override { return m_newEVMContractAddress; }
    void setNewEVMContractAddress(std::string newEVMContractAddress) override
    {
        m_newEVMContractAddress = std::move(newEVMContractAddress);
    }

    std::string_view toStringView(const std::string& it) const { return std::string_view(it); }

    gsl::span<std::string const> keyLocks() const override { return m_keyLocks; }

    std::vector<std::string> takeKeyLocks() override { return std::move(m_keyLocks); }

    void setKeyLocks(std::vector<std::string> keyLocks) override
    {
        m_keyLocks = std::move(keyLocks);
    }

    std::string_view keyLockAcquired() const override { return m_keyLockAcquired; }
    void setKeyLockAcquired(std::string keyLock) override { m_keyLockAcquired = keyLock; }

    bcos::crypto::HashType m_transactionHash;
    int64_t m_contextID = 0;
    int64_t m_seq = 0;
    int64_t m_executiveStateID;

    std::string m_origin;
    std::string m_from;
    std::string m_to;

    int64_t m_gasAvailable = 0;
    bcos::bytes m_data;

    std::optional<u256> m_createSalt;

    std::string m_message;
    std::vector<bcos::protocol::LogEntry> m_logEntries;
    std::string m_newEVMContractAddress;

    std::vector<std::string> m_keyLocks;
    std::string m_keyLockAcquired;

    int32_t m_status = 0;
    int32_t m_depth = 0;
    Type m_type = TXHASH;
    bool m_create = false;
    bool m_staticCall = false;
};

class NativeExecutionMessageFactory : public protocol::ExecutionMessageFactory
{
public:
    protocol::ExecutionMessage::UniquePtr createExecutionMessage() override
    {
        return std::make_unique<NativeExecutionMessage>();
    }
};
}  // namespace bcos::executor