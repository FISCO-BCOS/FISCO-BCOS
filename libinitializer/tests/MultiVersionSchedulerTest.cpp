// MultiVersionScheduler slot 3 (OP) routing and setVersion saturation.
#define BOOST_TEST_MODULE LibinitializerTests
#include <bcos-framework/storage/Entry.h>  // complete bcos::storage::Entry (fake's co_return nullopt)
#include <bcos-task/Wait.h>
#include <bcos-utilities/Exceptions.h>
#include <libinitializer/MultiVersionScheduler.h>
#include <libinitializer/OpRefusingStubScheduler.h>
#include <boost/exception/get_error_info.hpp>
#include <boost/test/unit_test.hpp>
#include <functional>
#include <memory>
#include <string>

namespace
{
using bcos::scheduler::SchedulerInterface;

// Fake scheduler: records which slot handled call/callAtBlock and the forwarded height.
struct FakeScheduler : public SchedulerInterface
{
    int id;
    std::shared_ptr<int> lastCallerId;  // shared across the 4 fakes; set by the selected one
    std::shared_ptr<bcos::protocol::BlockNumber> lastCallAtBlockNumber;
    explicit FakeScheduler(int i, std::shared_ptr<int> recorder,
        std::shared_ptr<bcos::protocol::BlockNumber> blockRecorder = {})
      : id(i), lastCallerId(recorder), lastCallAtBlockNumber(std::move(blockRecorder))
    {}
    void executeBlock(bcos::protocol::Block::Ptr, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> cb) override
    {
        cb({}, nullptr, false);
    }
    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> cb) override
    {
        cb({}, nullptr);
    }
    void call(bcos::protocol::Transaction::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)> cb) override
    {
        *lastCallerId = id;
        cb({}, nullptr);
    }
    void callAtBlock(bcos::protocol::Transaction::Ptr, bcos::protocol::BlockNumber blockNumber,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)> cb) override
    {
        *lastCallerId = id;
        if (lastCallAtBlockNumber)
            *lastCallAtBlockNumber = blockNumber;
        cb({}, nullptr);
    }
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(bcos::Error::Ptr)> cb) override
    {
        cb({});
    }
    void getCode(std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)> cb) override
    {
        cb({}, {});
    }
    void getABI(std::string_view, std::function<void(bcos::Error::Ptr, std::string)> cb) override
    {
        cb({}, {});
    }
    bcos::task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }
    void status(
        std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)> cb) override
    {
        cb({}, {});
    }
    void reset(std::function<void(bcos::Error::Ptr)> cb) override { cb({}); }
};

std::array<bcos::scheduler::SchedulerInterface::Ptr, 4> fakes(
    std::shared_ptr<int> recorder, std::shared_ptr<bcos::protocol::BlockNumber> blockRecorder = {})
{
    return {std::make_shared<FakeScheduler>(0, recorder, blockRecorder),
        std::make_shared<FakeScheduler>(1, recorder, blockRecorder),
        std::make_shared<FakeScheduler>(2, recorder, blockRecorder),
        std::make_shared<FakeScheduler>(3, recorder, blockRecorder)};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(MultiVersionSchedulerSuite)

BOOST_AUTO_TEST_CASE(Slot3Routing)
{
    auto recorder = std::make_shared<int>(-1);
    bcos::scheduler_v1::MultiVersionScheduler mvs(fakes(recorder));
    BOOST_CHECK_EQUAL(dynamic_cast<FakeScheduler&>(mvs.scheduler(3)).id, 3);
    // setVersion(3) selects slot 3; setVersion(4) saturates to slot 3.
    mvs.setVersion(3, {});
    mvs.call(nullptr, [](bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr) {});
    BOOST_CHECK_EQUAL(*recorder, 3);
    mvs.setVersion(4, {});
    mvs.call(nullptr, [](bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr) {});
    BOOST_CHECK_EQUAL(*recorder, 3);  // saturation: 4 >= size-1 -> newest (slot 3)
    mvs.setVersion(0, {});
    mvs.call(nullptr, [](bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr) {});
    BOOST_CHECK_EQUAL(*recorder, 0);  // low version routes to the first slot, not OP
}

/// callAtBlock must reach the version-selected slot WITH its block number — without the
/// MultiVersionScheduler override the interface default would silently drop the height and
/// serve call() (the latest state), a wrong answer instead of an error.
BOOST_AUTO_TEST_CASE(CallAtBlockRouting)
{
    auto recorder = std::make_shared<int>(-1);
    auto blockRecorder = std::make_shared<bcos::protocol::BlockNumber>(-1);
    bcos::scheduler_v1::MultiVersionScheduler mvs(fakes(recorder, blockRecorder));

    mvs.setVersion(3, {});
    mvs.callAtBlock(nullptr, 42, [](bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr) {});
    BOOST_CHECK_EQUAL(*recorder, 3);        // routed to the OP slot...
    BOOST_CHECK_EQUAL(*blockRecorder, 42);  // ...with the height intact (not dropped to call())

    mvs.setVersion(0, {});
    mvs.callAtBlock(nullptr, 7, [](bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr) {});
    BOOST_CHECK_EQUAL(*recorder, 0);
    BOOST_CHECK_EQUAL(*blockRecorder, 7);
}

/// Negative executor versions must be rejected at setVersion (ExecutorVersionNotSupported),
/// not routed or saturated silently — the runtime can call setVersion with a corrupted
/// system-config value.
BOOST_AUTO_TEST_CASE(NegativeVersionRejected)
{
    auto recorder = std::make_shared<int>(-1);
    bcos::scheduler_v1::MultiVersionScheduler mvs(fakes(recorder));
    try
    {
        mvs.setVersion(-1, {});
        BOOST_FAIL("expected ExecutorVersionNotSupported");
    }
    catch (bcos::scheduler_v1::ExecutorVersionNotSupported const& e)
    {
        auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
        BOOST_REQUIRE(comment != nullptr);
        BOOST_CHECK(comment->find("must be >= 0") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(OpRefusingStubRejectsAllSurfaces)
{
    bcos::initializer::OpRefusingStubScheduler stub;
    bool sawError = false;
    stub.executeBlock(
        nullptr, false, [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr, bool) {
            BOOST_REQUIRE(error);
            BOOST_CHECK_EQUAL(error->errorCode(), bcos::scheduler::SchedulerError::UnknownError);
            BOOST_CHECK(error->errorMessage().find("executor_version<3") != std::string::npos);
            sawError = true;
        });
    BOOST_CHECK(sawError);

    sawError = false;
    stub.call(nullptr, [&](bcos::Error::Ptr error, bcos::protocol::TransactionReceipt::Ptr) {
        BOOST_REQUIRE(error);
        BOOST_CHECK(error->errorMessage().find("OpRefusingStubScheduler") != std::string::npos);
        sawError = true;
    });
    BOOST_CHECK(sawError);

    BOOST_CHECK_THROW(bcos::task::syncWait(stub.getPendingStorageAt("", "", 0)), bcos::Error);
}

/// v1/v2 route to their own slots; the OP slot 3 is only reached at version >= 3.
BOOST_AUTO_TEST_CASE(VersionOneAndTwoRouting)
{
    auto recorder = std::make_shared<int>(-1);
    bcos::scheduler_v1::MultiVersionScheduler mvs(fakes(recorder));
    mvs.setVersion(1, {});
    mvs.call(nullptr, [](bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr) {});
    BOOST_CHECK_EQUAL(*recorder, 1);
    mvs.setVersion(2, {});
    mvs.call(nullptr, [](bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr) {});
    BOOST_CHECK_EQUAL(*recorder, 2);
}

BOOST_AUTO_TEST_SUITE_END()
