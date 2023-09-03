#include "RPCClient.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-utilities/Exceptions.h"
#include <boost/throw_exception.hpp>

struct InvalidHostPortStringError : public bcos::Exception
{
};

bcostars::RPCPrx& bcos::sdk::RPCClient::rpcProxy()
{
    return m_rpcProxy;
}
std::string bcos::sdk::RPCClient::toConnectionString(const std::vector<std::string>& hostAndPorts)
{
    std::string connectionString = "fiscobcos.rpc.RPCObj@";
    std::vector<std::string> items;
    for (auto const& host : hostAndPorts)
    {
        items.clear();
        boost::split(items, host, boost::is_any_of(":"), boost::token_compress_on);

        if (items.size() < 2)
        {
            BOOST_THROW_EXCEPTION(InvalidHostPortStringError{});
        }
        connectionString += "tcp -h " + items[0] + " -p " + items[1] + ":";
    }
    return connectionString.substr(0, connectionString.size() - 1);
}

void bcos::sdk::RPCClient::onMessage(tars::ReqMessagePtr message)
{
    auto& callbackBase = dynamic_cast<detail::TarsCallback&>(*message->callback);
    callbackBase.promise().set_value(std::move(message));

    if (callbackBase.callback() != nullptr)
    {
        callbackBase.callback()->onMessage();
    }
}
bcos::sdk::RPCClient::RPCClient(bcos::sdk::Config const& config)
{
    m_communicator.setProperty("sendqueuelimit", std::to_string(config.sendQueueSize));
    m_rpcProxy = m_communicator.stringToProxy<bcostars::RPCPrx>(config.connectionString);
    m_rpcProxy->tars_set_custom_callback(&RPCClient::onMessage);
    m_rpcProxy->tars_async_timeout(config.timeoutMs);
}
std::string bcos::sdk::RPCClient::generateNonce()
{
    struct NoncePrefix
    {
        uint32_t rand1;
        uint32_t rand2;
        uint32_t rand3;
        uint32_t requestid;
    };
    static unsigned rand1 = std::random_device{}();
    static unsigned rand2 = std::random_device{}();
    static thread_local std::mt19937 randomDevice{std::random_device{}()};

    std::string nonce;
    nonce.resize(sizeof(NoncePrefix));
    auto* noncePtr = reinterpret_cast<NoncePrefix*>(nonce.data());
    noncePtr->rand1 = rand1;
    noncePtr->rand2 = rand2;
    noncePtr->rand3 = randomDevice();
    noncePtr->requestid = m_rpcProxy->tars_gen_requestid();

    return nonce;
}

bcos::sdk::SendTransaction::SendTransaction(RPCClient& rpcClient) : Handle(rpcClient) {}
bcos::sdk::SendTransaction& bcos::sdk::SendTransaction::send(
    const bcos::protocol::Transaction& transaction)
{
    auto const& tarsTransaction =
        dynamic_cast<bcostars::protocol::TransactionImpl const&>(transaction);

    std::promise<tars::ReqMessagePtr> promise;
    setFuture(promise.get_future());

    auto tarsCallback = std::make_unique<detail::TarsCallback>(callback(), std::move(promise));
    rpcClient().rpcProxy()->async_sendTransaction(tarsCallback.release(), tarsTransaction.inner());

    return *this;
}

bcos::sdk::Call::Call(RPCClient& rpcClient) : Handle(rpcClient) {}
bcos::sdk::Call& bcos::sdk::Call::send(const protocol::Transaction& transaction)
{
    auto const& tarsTransaction =
        dynamic_cast<bcostars::protocol::TransactionImpl const&>(transaction);

    std::promise<tars::ReqMessagePtr> promise;
    setFuture(promise.get_future());

    auto tarsCallback = std::make_unique<detail::TarsCallback>(callback(), std::move(promise));
    rpcClient().rpcProxy()->async_call(tarsCallback.release(), tarsTransaction.inner());

    return *this;
}

bcos::sdk::BlockNumber::BlockNumber(RPCClient& rpcClient) : Handle(rpcClient) {}
bcos::sdk::BlockNumber& bcos::sdk::BlockNumber::send()
{
    std::promise<tars::ReqMessagePtr> promise;
    setFuture(promise.get_future());

    auto tarsCallback = std::make_unique<detail::TarsCallback>(callback(), std::move(promise));
    rpcClient().rpcProxy()->async_blockNumber(tarsCallback.release());

    return *this;
}
