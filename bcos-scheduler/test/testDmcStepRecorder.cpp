#include "DmcStepRecorder.h"
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>


using namespace bcos::scheduler;

namespace bcos::test
{
struct DmcStepRecorderFixture
{
    DmcStepRecorderFixture()
    {
        auto start = utcTime();
        for (size_t id = 0; utcTime() - start < m_generateCntLimit; id++)
        {
            auto message = std::make_unique<bcos::executor::NativeExecutionMessage>();

            message->setStaticCall(bool(id % 2));
            message->setType(protocol::ExecutionMessage::Type(id % 6));
            message->setContextID(id);
            message->setSeq(id * id * ~id % (id + 1));
            message->setOrigin("aabbccdd");
            message->setFrom("eeffaabb");
            message->setTo("ccddeeff");
            m_sendMessages.push_back(std::move(message));
        }

        start = utcTime();
        for (size_t id = 0; utcTime() - start < m_generateCntLimit; id++)
        {
            auto message = std::make_unique<bcos::executor::NativeExecutionMessage>();

            message->setStaticCall(bool(id % 2));
            message->setType(protocol::ExecutionMessage::Type(id % 6));
            message->setContextID(id);
            message->setSeq(id * id * ~id % (id + 1));
            message->setOrigin("aabbccdd");
            message->setFrom("eeffaabb");
            message->setTo("ccddeeff");
            m_rcvMessages.emplace_back(std::move(message));
        }
    }

    uint64_t m_generateCntLimit = 500;  // 500ms
    std::vector<protocol::ExecutionMessage::UniquePtr> m_sendMessages;
    std::vector<protocol::ExecutionMessage::UniquePtr> m_rcvMessages;
    std::string m_address = "aacc0011";
};

BOOST_FIXTURE_TEST_SUITE(TestDmcStepRecorder, DmcStepRecorderFixture)

BOOST_AUTO_TEST_CASE(Test)
{
    size_t totalLoop = 10;
    std::cout << "[0] Dmc recorder test start for:" << m_sendMessages.size() << "-"
              << m_rcvMessages.size() << std::endl;
    auto start = utcTime();
    DmcStepRecorder::Ptr recorder = std::make_shared<DmcStepRecorder>();
    for (size_t i = 0; i < totalLoop; i++)
    {
        recorder->recordSends(m_address, m_sendMessages);
        recorder->recordReceives(m_address, m_rcvMessages);
        recorder->nextDmcRound();
    }
    std::string res1 = recorder->dumpAndClearChecksum();
    std::cout << "[1]:" << res1 << " Use " << (utcTime() - start) << "ms from start" << std::endl;

    start = utcTime();
    for (size_t i = 0; i < totalLoop; i++)
    {
        for (size_t id = 0; id < m_sendMessages.size(); id++)
        {
            recorder->recordSend(m_address, id, m_sendMessages[id]);
        }

        for (size_t id = 0; id < m_rcvMessages.size(); id++)
        {
            recorder->recordReceive(m_address, id, m_rcvMessages[id]);
        }
        recorder->nextDmcRound();
    }
    std::string res2 = recorder->dumpAndClearChecksum();

    std::cout << "[2]:" << res2 << " Use " << (utcTime() - start) << "ms from last" << std::endl;

    BOOST_CHECK(res1 == res2);
}

BOOST_AUTO_TEST_CASE(ShortAddress)
{
    // A Solidity contract-creation message carries an empty to (BlockExecutive sets create and
    // never calls setTo), so every deploy executed through DMC reaches the short-address path of
    // getAddressChecksum. Both cases pin the recorder output against a golden value. The empty
    // address pins determinism: its padding loop is a no-op, so what it catches is a regression
    // to the unconditional four-byte read (a same-process self-comparison would not, and ASan
    // stays silent because an empty std::string's data() points into live small-string storage).
    // The non-empty short address is the case that actually pins the zero-padding: reverting the
    // padding loop changes its value and turns this red.
    auto makeMessage = [](std::string to = "") {
        auto message = std::make_unique<bcos::executor::NativeExecutionMessage>();
        message->setStaticCall(false);
        message->setType(protocol::ExecutionMessage::Type(0));
        message->setContextID(1);
        message->setSeq(1);
        message->setOrigin("aabbccdd");
        message->setFrom("eeffaabb");
        message->setTo(to);
        return message;
    };

    DmcStepRecorder::Ptr recorder = std::make_shared<DmcStepRecorder>();
    recorder->recordSend(m_address, 0, makeMessage());
    recorder->nextDmcRound();
    BOOST_CHECK_EQUAL(recorder->dumpAndClearChecksum(), "59758e84:59758e83:0:1");

    recorder->recordSend(m_address, 0, makeMessage("ab"));
    recorder->nextDmcRound();
    BOOST_CHECK_EQUAL(recorder->dumpAndClearChecksum(), "31bf8e84:31bf8e83:0:1");
}


BOOST_AUTO_TEST_CASE(scheduleOutRecordingIsOptInAndClearResetsDiagnostics)
{
    DmcStepRecorder recorder;
    auto record = [&] {
        recorder.recordScheduleOut(std::string(42, 'a'), 1, 2, 3, 4, std::string(42, 'b'), false,
            bcos::protocol::ExecutionMessage::MESSAGE);
    };
    record();
    BOOST_CHECK(recorder.scheduleOuts().empty());
    recorder.enableScheduleOutRecording();
    record();
    auto records = recorder.scheduleOuts();
    BOOST_REQUIRE_EQUAL(records.size(), 1);
    BOOST_CHECK_EQUAL(records.front().target, std::string(42, 'b'));
    // Block completion dumps checksums before diagnostics are inspected by tests.
    recorder.dumpAndClearChecksum();
    BOOST_CHECK_EQUAL(recorder.scheduleOuts().size(), 1);
    recorder.recordSchedulerStampViolation("contract", 1, 0);
    recorder.clear();
    BOOST_CHECK(recorder.scheduleOuts().empty());
    BOOST_CHECK_EQUAL(recorder.schedulerStampViolations(), 0);
    record();
    BOOST_CHECK_EQUAL(recorder.scheduleOuts().size(), 1);
    recorder.enableScheduleOutRecording(false);
    record();
    BOOST_CHECK_EQUAL(recorder.scheduleOuts().size(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test