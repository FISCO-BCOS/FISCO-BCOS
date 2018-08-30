#include <boost/test/unit_test.hpp>
#include <json/json.h>

#include <unittest/Common.h>
#include <libdevcore/Common.h>
#include <libchannelserver/ChannelMessage.h>

using namespace dev;
using namespace dev::channel;

namespace test_ChannelMessage {

struct ChannelMessageFixture {
	ChannelMessageFixture() {
		auto message = std::make_shared<dev::channel::ChannelMessage>();

		message->setType(0x20);
		message->setSeq(std::string(32, '0'));
		message->setResult(1);

		std::string s = "abcd";
		data.assign(s.begin(), s.end());
		message->setData(data.data(), data.size());
		message->encode(amopRequest);

		TopicChannelMessage::Ptr topicMessage = std::make_shared<dev::channel::TopicChannelMessage>();
		topicMessage->setType(0x30);
		topicMessage->setSeq(std::string(32, '0'));
		topicMessage->setResult(0);
		topicMessage->setTopic("test_topic");
		topicMessage->setData(data.data(), data.size());
		topicMessage->encode(topicRequest);
	}

	~ChannelMessageFixture() {
		//什么也不做
	}

	std::string httpRequest = "POST / HTTP/1.1\r\n"
				"User-Agent: curl/7.29.0\r\n"
				"Host: 127.0.0.1:5566\r\n"
				"Accept: */*\r\n"
				"Content-Length: 67\r\n"
				"Content-Type: application/x-www-form-urlencoded\r\n"
				"\r\n"
				"{\"jsonrpc\":\"2.0\",\"method\":\"web3_clientVersion\",\"params\":[],\"id\":67}";

	bytes amopRequest;
	bytes topicRequest;

	bytes data;
};

BOOST_FIXTURE_TEST_SUITE(ChannelMessage, ChannelMessageFixture)

BOOST_AUTO_TEST_CASE(normalHttp) {
#if 0
	ChannelMessage::Ptr message = std::make_shared<ChannelMessage>();
	ssize_t r = message->decode((const byte*)httpRequest.c_str(), httpRequest.size());


	BOOST_TEST(r == 67);
	BOOST_TEST(message->dataSize() == 67);
	BOOST_TEST(message->type() == 0x20);
	BOOST_TEST(message->seq() == std::string(32, '0'));
	BOOST_TEST(std::string((const char*)message->data(), message->dataSize()) ==
			"{\"jsonrpc\":\"2.0\",\"method\":\"web3_clientVersion\",\"params\":[],\"id\":67}");
#endif
}

BOOST_AUTO_TEST_CASE(wrongHttp) {
#if 0
	std::string wrongRequest = "POST / HTTP/1.1\r\n"
			"User-Agent: curl/7.29.0\r\n"
			"Host: 127.0.0.1:5566\r\n\r\r\r"
			"Accept: */*\n"
			"Content-Length: 69\r\n"
			"Content-Type: application/x-www-form-urlencoded\r\n"
			"\r\n\r\n"
			"{\"jsonrpc\":\"2.0\",\"method\":\"web3_clientVersion\",\"params\":[],\"id\":67}";

	ChannelMessage::Ptr message = std::make_shared<ChannelMessage>();
	ssize_t r = message->decode((const byte*)wrongRequest.c_str(), wrongRequest.size());

	BOOST_TEST(r == -1);
#endif
}

BOOST_AUTO_TEST_CASE(splitHttp) {
#if 0
	ChannelMessage::Ptr message = std::make_shared<ChannelMessage>();
	ssize_t r = message->decode((const byte*)httpRequest.c_str(), httpRequest.size() - httpRequest.size() / 3);

	BOOST_TEST(r == 0);

	r = message->decode((const byte*)httpRequest.c_str(), httpRequest.size());

	BOOST_TEST(r == 67);
	BOOST_TEST(message->dataSize() == 67);
	BOOST_TEST(message->type() == 0x20);
	BOOST_TEST(message->seq() == std::string(32, '0'));
	BOOST_TEST(std::string((const char*)message->data(), message->dataSize()) ==
			"{\"jsonrpc\":\"2.0\",\"method\":\"web3_clientVersion\",\"params\":[],\"id\":67}");
#endif
}

BOOST_AUTO_TEST_CASE(normalAMOP) {
	auto message = std::make_shared<dev::channel::ChannelMessage>();
	ssize_t r = message->decode((const byte*)amopRequest.data(), amopRequest.size());

	BOOST_TEST(r == amopRequest.size());
	BOOST_TEST(message->type() == 0x20);
	BOOST_TEST(message->seq() == std::string(32, '0'));
	BOOST_TEST(message->result() == 1);

	bytes d(message->data(), message->data() + message->dataSize());
	BOOST_TEST(d == data);
}

BOOST_AUTO_TEST_CASE(normalTopic) {
	TopicChannelMessage::Ptr message = std::make_shared<TopicChannelMessage>();
	ssize_t r = message->decode((const byte*)topicRequest.data(), topicRequest.size());

	BOOST_TEST(r == topicRequest.size());
	BOOST_TEST(message->type() == 0x30);
	BOOST_TEST(message->seq() == std::string(32, '0'));
	BOOST_TEST(message->result() == 0);
	BOOST_TEST(message->topic() == "test_topic");

	bytes d(message->data(), message->data() + message->dataSize());
	BOOST_TEST(d == data);
}

BOOST_AUTO_TEST_CASE(topicFromMessage) {
	auto rawMessage = std::make_shared<dev::channel::ChannelMessage>();
	ssize_t r = rawMessage->decode((const byte*) topicRequest.data(), topicRequest.size());

	TopicChannelMessage::Ptr message = std::make_shared<TopicChannelMessage>(rawMessage.get());
	BOOST_TEST(r == topicRequest.size());
	BOOST_TEST(message->type() == 0x30);
	BOOST_TEST(message->seq() == std::string(32, '0'));
	BOOST_TEST(message->result() == 0);
	BOOST_TEST(message->topic() == "test_topic");

	bytes d(message->data(), message->data() + message->dataSize());
	BOOST_TEST(d == data);
}

BOOST_AUTO_TEST_SUITE_END()

}
