#pragma once

#include <bcos-concepts/scheduler/Scheduler.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <bcos-task/Task.h>
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

    task::Task<void> impl_call(bcos::concepts::transaction::Transaction auto const& transaction,
        bcos::concepts::receipt::TransactionReceipt auto& receipt)
    {
        auto transactionImpl = std::make_shared<bcostars::protocol::TransactionImpl>(m_cryptoSuite,
            [&transaction]() { return const_cast<bcostars::Transaction*>(&transaction); });

        struct Awaitable : public CO_STD::suspend_always
        {
            Awaitable(decltype(transactionImpl)& transactionImpl, SchedulerType& scheduler,
                std::remove_cvref_t<decltype(receipt)>& receipt)
              : m_transactionImpl(transactionImpl), m_scheduler(scheduler), m_receipt(receipt)
            {}

            void await_suspend(CO_STD::coroutine_handle<task::Task<void>::promise_type> handle)
            {
                bcos::concepts::getRef(m_scheduler)
                    .call(std::move(m_transactionImpl),
                        [this, m_handle = std::move(handle)](Error::Ptr&& error,
                            protocol::TransactionReceipt::Ptr&& transactionReceipt) mutable {
                            if (!error)
                            {
                                auto tarsImpl = std::dynamic_pointer_cast<
                                    bcostars::protocol::TransactionReceiptImpl>(transactionReceipt);

                                m_receipt = std::move(
                                    const_cast<bcostars::TransactionReceipt&>(tarsImpl->inner()));
                            }
                            else
                            {
                                m_error = std::move(error);
                            }

                            m_handle.resume();
                        });
            }

            decltype(transactionImpl)& m_transactionImpl;
            SchedulerType& m_scheduler;
            std::remove_cvref_t<decltype(receipt)>& m_receipt;
            Error::Ptr m_error;
        };

        Awaitable awaitable(transactionImpl, m_scheduler, receipt);
        co_await awaitable;

        auto& error = awaitable.m_error;
        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        co_return;
    }


    SchedulerType m_scheduler;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace bcos::scheduler