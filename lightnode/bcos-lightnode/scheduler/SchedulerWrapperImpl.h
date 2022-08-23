#pragma once

#include <bcos-concepts/scheduler/Scheduler.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <boost/throw_exception.hpp>
#include <memory>

namespace bcos::scheduler
{
template <class SchedulerType>
class SchedulerWrapperImpl
  : public bcos::concepts::scheduler::SchedulerBase<SchedulerWrapperImpl<SchedulerType>>
{
    friend bcos::concepts::scheduler::SchedulerBase<SchedulerWrapperImpl<SchedulerType>>;

public:
    SchedulerWrapperImpl(SchedulerType scheduler, bcos::crypto::CryptoSuite::Ptr cryptoSuite)
      : m_scheduler(std::move(scheduler)), m_cryptoSuite(std::move(cryptoSuite))
    {}

private:
    auto& scheduler() { return bcos::concepts::getRef(m_scheduler); }

    void impl_call(bcos::concepts::transaction::Transaction auto const& transaction,
        bcos::concepts::receipt::TransactionReceipt auto& receipt)
    {
        auto transactionImpl = std::make_shared<bcostars::protocol::TransactionImpl>(m_cryptoSuite,
            [&transaction]() { return const_cast<bcostars::Transaction*>(&transaction); });

        // must ensure the lifetime of promise
        auto promise = std::make_shared<std::promise<Error::Ptr>>();
        // Note: can't call get_future after called get()
        auto future = promise->get_future();
        scheduler().call(
            transactionImpl, [promise, &receipt](Error::Ptr&& error,
                                 protocol::TransactionReceipt::Ptr&& transactionReceipt) {
                try
                {
                    if (!error)
                    {
                        auto tarsImpl =
                            std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(
                                transactionReceipt);

                        receipt = tarsImpl->inner();
                    }
                    // Note: promise set_value can't been called multiple times, otherwise will
                    // throw future_error exception
                    promise->set_value(std::move(error));
                }
                catch (std::future_error const& e)
                {
                    BCOS_LOG(INFO)
                        << LOG_DESC("impl_call: the callback has already been called for timeout")
                        << LOG_KV("msg", boost::diagnostic_information(e));
                }
            });
        if (future.wait_for(std::chrono::seconds(c_callTimeout)) == std::future_status::timeout)
        {
            auto errorMsg = "call timeout";
            BCOS_LOG(ERROR) << LOG_DESC("SchedulerWrapperImpl") << errorMsg;
            auto error = std::make_shared<Error>(-1, errorMsg);
            BOOST_THROW_EXCEPTION(*error);
        }

        auto error = future.get();
        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }
    }


    SchedulerType m_scheduler;
    int c_callTimeout = 10;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace bcos::scheduler