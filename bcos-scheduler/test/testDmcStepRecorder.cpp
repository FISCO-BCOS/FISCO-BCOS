#include "DmcStepRecorder.h"
#include <bcos-framework/interfaces/executor/NativeExecutionMessage.h>
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
}
}  // namespace bcos::test