#include "RPCServer.h"
#include "../Common.h"
#include "Config.h"
#include "bcos-concepts/Serialize.h"
#include "bcos-tars-protocol/impl/TarsSerializable.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-task/Wait.h"
#include <boost/exception/diagnostic_information.hpp>
#include <memory>
#include <variant>

size_t bcos::rpc::Params::PtrHash::hash(const tars::CurrentPtr& key)
{
    return std::hash<void*>{}((void*)key.get());
}
bool bcos::rpc::Params::PtrHash::equal(const tars::CurrentPtr& lhs, const tars::CurrentPtr& rhs)
{
    return lhs == rhs;
}

void bcos::rpc::RPCServer::initialize() {}
void bcos::rpc::RPCServer::destroy() {}

bcostars::Error bcos::rpc::RPCServer::handshake(
    const std::vector<std::string>& topics, tars::TarsCurrentPtr current)
{
    decltype(m_params.sessions)::accessor accessor;
    m_params.sessions.find(accessor, current);

    if (accessor.empty())
    {
        m_params.sessions.emplace(
            accessor, current, std::move(const_cast<std::vector<std::string>&>(topics)));
    }
    else
    {
        accessor->second = std::move(const_cast<std::vector<std::string>&>(topics));
    }

    return {};
}

bcostars::Error bcos::rpc::RPCServer::call(const bcostars::Transaction& request,
    bcostars::TransactionReceipt& response, tars::TarsCurrentPtr current)
{
    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        std::stringstream ss;
        request.displaySimple(ss);
        RPC_LOG(TRACE) << "RPC call request" << ss.str();
    }

    current->setResponse(false);
    auto transaction = std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = std::move(const_cast<bcostars::Transaction&>(request))]() mutable {
            return &inner;
        });

    m_params.node->scheduler()->call(std::move(transaction),
        [current](Error::Ptr const& error,
            protocol::TransactionReceipt::Ptr const& transactionReceiptPtr) {
            bcostars::Error tarsError;
            try
            {
                if (error)
                {
                    RPC_LOG(ERROR)
                        << "call got bcos::Error: " << boost::diagnostic_information(*error);
                    tarsError.errorCode = static_cast<int32_t>(error->errorCode());
                    tarsError.errorMessage = error->errorMessage();

                    bcos::rpc::RPCServer::async_response_call(current, tarsError, {});
                    return;
                }

                auto const& receipt =
                    dynamic_cast<bcostars::protocol::TransactionReceiptImpl const&>(
                        *transactionReceiptPtr);
                bcos::rpc::RPCServer::async_response_call(current, tarsError, receipt.inner());
            }
            catch (std::exception& e)
            {
                RPC_LOG(ERROR) << "call got std::exception: " << boost::diagnostic_information(e);
                tarsError.errorCode = -1;
                tarsError.errorMessage = e.what();
                bcos::rpc::RPCServer::async_response_call(current, tarsError, {});
            }
        });

    return {};
}

bcostars::Error bcos::rpc::RPCServer::sendTransaction(const bcostars::Transaction& request,
    bcostars::TransactionReceipt& response, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto transaction = std::make_shared<bcostars::protocol::TransactionImpl>(
        [inner = std::move(const_cast<bcostars::Transaction&>(request))]() mutable {
            return &inner;
        });

    bcos::task::wait([](decltype(this) self, decltype(transaction) transaction,
                         tars::TarsCurrentPtr current) -> task::Task<void> {
        bcostars::Error error;
        try
        {
            auto& txpool = self->m_params.node->txpoolRef();
            txpool.broadcastTransaction(*transaction);
            auto submitResult = co_await txpool.submitTransaction(std::move(transaction));
            const auto& receipt = dynamic_cast<bcostars::protocol::TransactionReceiptImpl const&>(
                *submitResult->transactionReceipt());

            bcos::rpc::RPCServer::async_response_sendTransaction(current, error, receipt.inner());
            co_return;
        }
        catch (bcos::Error& e)
        {
            RPC_LOG(ERROR) << "sendTransaction got bcos::Error: "
                           << boost::diagnostic_information(e);
            error.errorCode = static_cast<int32_t>(e.errorCode());
            error.errorMessage = e.errorMessage();
        }
        catch (std::exception& e)
        {
            RPC_LOG(ERROR) << "sendTransaction got std::exception: "
                           << boost::diagnostic_information(e);
            error.errorCode = -1;
            error.errorMessage = e.what();
        }
        bcos::rpc::RPCServer::async_response_sendTransaction(current, error, {});
    }(this, std::move(transaction), current));

    return {};
}

bcostars::Error bcos::rpc::RPCServer::blockNumber(long& number, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_params.node->ledger()->asyncGetBlockNumber(
        [current](const Error::Ptr& error, protocol::BlockNumber blockNumber) {
            if (error)
            {
                bcostars::Error errorMessage;
                errorMessage.errorCode = static_cast<tars::Int32>(error->errorCode());
                errorMessage.errorMessage = error->errorMessage();

                bcos::rpc::RPCServer::async_response_blockNumber(current, errorMessage, 0);
                return;
            }

            bcos::rpc::RPCServer::async_response_blockNumber(current, {}, blockNumber);
        });
    return {};
}

int bcos::rpc::RPCServer::doClose(tars::CurrentPtr current)
{
    m_params.sessions.erase(current);
    return bcostars::RPC::doClose(current);
}

void bcos::rpc::RPCApplication::initialize()
{
    addServantWithParams<RPCServer, Params>(
        tars::ServerConfig::Application + "." + tars::ServerConfig::ServerName + "." + "RPCObj",
        m_params);
}

void bcos::rpc::RPCApplication::destroyApp() {}

void bcos::rpc::RPCApplication::pushBlockNumber(long blockNumber)
{
    for (auto& [current, _] : m_params.sessions)
    {}
}

std::string bcos::rpc::RPCApplication::generateTarsConfig(
    std::string_view host, uint16_t port, size_t threadCount)
{
    constexpr static std::string_view HOST_PLACEHOLDER = "[[TARS_HOST]]";
    constexpr static std::string_view PORT_PLACEHOLDER = "[[TARS_PORT]]";
    constexpr static std::string_view THREAD_COUNT_PLACEHOLDER = "[[TARS_THREAD_COUNT]]";

    std::string config{TARS_CONFIG_TEMPLATE};
    config.replace(config.find(HOST_PLACEHOLDER), HOST_PLACEHOLDER.size(), host);
    config.replace(config.find(PORT_PLACEHOLDER), PORT_PLACEHOLDER.size(), std::to_string(port));
    config.replace(config.find(THREAD_COUNT_PLACEHOLDER), THREAD_COUNT_PLACEHOLDER.size(),
        std::to_string(threadCount));

    return config;
}
